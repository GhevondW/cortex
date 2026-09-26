// A scheduler may be driven from inside another scheduler's fiber (e.g. a
// component that owns its own scheduler, called from application fibers).
// Scheduler::Current() must always name the innermost running scheduler and
// be restored when the inner one stops running.

#include <cortex/tiny_fiber/tiny_fiber.hpp>

#include <gtest/gtest.h>

namespace tf = cortex::tiny_fiber;

TEST(TinyFiberNested, OuterFiberCanYieldAfterSteppingInnerScheduler) {
    bool outer_yield_ok = false;
    tf::Scheduler::Run([&] {
        auto inner = tf::Scheduler::Create([] {
            tf::Yield();
        });
        while (inner->Step()) {
        }
        tf::Yield(); // must yield the OUTER fiber
        outer_yield_ok = true;
    });
    EXPECT_TRUE(outer_yield_ok);
}

TEST(TinyFiberNested, CurrentIsInnermostInsideInnerFiber) {
    // Values are recorded and asserted outside the fibers so that a throwing
    // Current() cannot hide inside a fiber.
    tf::Scheduler* outer = nullptr;
    tf::Scheduler* inner_ptr = nullptr;
    tf::Scheduler* seen_inside = nullptr;
    tf::Scheduler* after_inner = nullptr;
    tf::Scheduler::Run([&] {
        outer = tf::Scheduler::TryCurrent();
        auto inner = tf::Scheduler::Create([&] {
            seen_inside = tf::Scheduler::TryCurrent();
        });
        inner_ptr = inner.get();
        while (inner->Step()) {
        }
        after_inner = tf::Scheduler::TryCurrent();
    });
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(seen_inside, inner_ptr);
    EXPECT_EQ(after_inner, outer);
}

TEST(TinyFiberNested, RunInsideFiber) {
    int inner_result = 0;
    tf::Scheduler::Run([&] {
        tf::Scheduler::Run([&] {
            auto f = tf::Spawn([] {
                tf::Yield();
                return 5;
            });
            inner_result = f.Get();
        });
        tf::Yield();
    });
    EXPECT_EQ(inner_result, 5);
}

TEST(TinyFiberNested, DestroyingInnerSchedulerInsideFiberRestoresOuter) {
    tf::Scheduler* outer = nullptr;
    tf::Scheduler* after_teardown = nullptr;
    tf::Scheduler::Run([&] {
        outer = tf::Scheduler::TryCurrent();
        {
            auto inner = tf::Scheduler::Create([] {
                tf::Yield();
            });
            inner->Step(); // leave a fiber unfinished so teardown has work to do
        }
        after_teardown = tf::Scheduler::TryCurrent();
    });
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(after_teardown, outer);
}

TEST(TinyFiberNested, TryCurrentIsNullOutsideFibers) {
    EXPECT_EQ(tf::Scheduler::TryCurrent(), nullptr);
}
