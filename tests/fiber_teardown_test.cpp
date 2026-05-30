// Regression tests for two scheduler/future teardown bugs:
//
//   1. Future use-after-free: a Future that outlives its Scheduler must not
//      dereference the freed scheduler from ~Future/Wait/Get/IsReady.
//
//   2. ForcedUnwind leak: when a parked fiber is force-unwound during scheduler
//      teardown, the internal cortex::detail::ForcedUnwind must NOT be captured
//      into the fiber's FutureState and surfaced to user code via Wait()/Get().
//
// Test 1 is best run under AddressSanitizer (CORTEX_USE_SANITIZERS=ON): without
// the fix it aborts with a heap-use-after-free; with the fix it runs clean.

#include <cortex/detail/forced_unwind.hpp>
#include <cortex/tiny_fiber/future.hpp>
#include <cortex/tiny_fiber/scheduler.hpp>
#include <cortex/tiny_fiber/yield.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <optional>

namespace tf = cortex::tiny_fiber;

TEST(FiberTeardown, FutureOutlivingSchedulerIsSafeToDestroy) {
    std::optional<tf::Future<int>> escaped;
    {
        auto scheduler = tf::Scheduler::Create([&escaped] {
            auto f = tf::Spawn([] {
                tf::Yield();
                tf::Yield();
                return 7;
            });
            // Move the Future out into storage that outlives the scheduler.
            escaped.emplace(std::move(f));
        });
        scheduler->Step(); // entry fiber runs, moves the future out; worker not done
    } // scheduler destroyed here

    // Destroying the surviving Future must not touch the freed scheduler.
    EXPECT_NO_THROW(escaped.reset());
}

TEST(FiberTeardown, IsReadyOnSurvivingFutureDoesNotUseFreedScheduler) {
    std::optional<tf::Future<int>> escaped;
    {
        auto scheduler = tf::Scheduler::Create([&escaped] {
            escaped.emplace(tf::Spawn([] {
                tf::Yield();
                return 1;
            }));
        });
        scheduler->Step();
    }
    // IsReady() must report "ready" rather than dereferencing the dead scheduler.
    bool ready = false;
    EXPECT_NO_THROW(ready = escaped->IsReady());
    EXPECT_TRUE(ready);
    escaped.reset();
}

TEST(FiberTeardown, ForcedUnwindNotDeliveredToSurvivingFuture) {
    // Two fibers that Wait() on each other (a deadlock cycle). After the
    // scheduler stops, each re-parks in Future::Wait during the drain and is
    // force-unwound by fibers_.clear(). Their captured exception must not be
    // the internal ForcedUnwind.
    auto af = std::make_shared<std::optional<tf::Future<void>>>();
    auto bf = std::make_shared<std::optional<tf::Future<void>>>();
    {
        auto scheduler = tf::Scheduler::Create([af, bf] {
            *bf = tf::Spawn([af]() mutable {
                for (;;) {
                    if (af->has_value()) {
                        (**af).Wait();
                    } else {
                        tf::Yield();
                    }
                }
            });
            *af = tf::Spawn([bf]() mutable {
                for (;;) {
                    if (bf->has_value()) {
                        (**bf).Wait();
                    } else {
                        tf::Yield();
                    }
                }
            });
            tf::Yield();
        });
        for (int i = 0; i < 30; ++i) {
            scheduler->Step();
        }
        // Keep af/bf valid so the fibers stay deadlocked and force-unwind.
    } // scheduler destroyed → A and B force-unwound

    bool forced_unwind_leaked = false;
    try {
        if (af->has_value()) {
            (**af).Wait();
        }
    } catch (const cortex::detail::ForcedUnwind&) {
        forced_unwind_leaked = true;
    } catch (...) {
        // Any other outcome is acceptable for this regression.
    }
    EXPECT_FALSE(forced_unwind_leaked);

    af->reset();
    bf->reset();
}
