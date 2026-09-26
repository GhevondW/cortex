// Future semantics: where Get()/Wait() may be called from, what a Future
// holds when its fiber could not finish, and detached / fire-and-forget
// fibers.

#include <cortex/tiny_fiber/tiny_fiber.hpp>

#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace tf = cortex::tiny_fiber;

TEST(TinyFiberFuture, GetOutsideFiberOnUnfinishedResultThrowsClearError) {
    std::optional<tf::Future<int>> escaped;
    auto scheduler = tf::Scheduler::Create([&] {
        escaped.emplace(tf::Spawn([] {
            tf::Yield();
            return 1;
        }));
    });
    scheduler->Step(); // entry spawns the child; the child has not run yet

    std::string message;
    try {
        (void)escaped->Get();
    } catch (const std::logic_error& e) {
        message = e.what();
    }
    EXPECT_NE(message.find("not a fiber"), std::string::npos) << "got: " << message;

    while (scheduler->Step()) {
    }
    ASSERT_TRUE(escaped->IsReady());
    EXPECT_EQ(escaped->Get(), 1); // finished results can be read from anywhere
}

TEST(TinyFiberFuture, WaitOutsideFiberOnUnfinishedResultThrowsClearError) {
    std::optional<tf::Future<void>> escaped;
    auto scheduler = tf::Scheduler::Create([&] { escaped.emplace(tf::Spawn([] { tf::Yield(); })); });
    scheduler->Step();
    EXPECT_THROW(escaped->Wait(), std::logic_error);
    while (scheduler->Step()) {
    }
    EXPECT_NO_THROW(escaped->Wait());
}

TEST(TinyFiberFuture, UnfinishedFiberAtTeardownFailsWithStoppingError) {
    std::optional<tf::Future<int>> escaped;
    {
        auto scheduler = tf::Scheduler::Create([&] {
            escaped.emplace(tf::Spawn([] {
                tf::Mutex mutex;
                tf::ConditionVariable cv;
                auto guard = tf::Lock(mutex);
                cv.Wait(guard); // nobody notifies: still parked at teardown
                return 1;
            }));
        });
        scheduler->Step();
        scheduler->Step();
    }
    ASSERT_TRUE(escaped->IsReady());
    EXPECT_THROW((void)escaped->Get(), tf::CancelledError); // SchedulerStoppingError is-a CancelledError
}

TEST(TinyFiberFuture, SchedulerStoppingErrorIsACancelledError) {
    const tf::SchedulerStoppingError stopping;
    const tf::CancelledError* as_cancelled = &stopping;
    EXPECT_NE(as_cancelled, nullptr);
}

TEST(TinyFiberFuture, WaitIsStopAware) {
    bool threw = false;
    auto scheduler = tf::Scheduler::Create([&] {
        auto child = tf::Spawn([] {
            for (;;) {
                tf::Yield();
            }
        });
        try {
            child.Wait();
        } catch (const tf::SchedulerStoppingError&) {
            threw = true;
        }
    });
    scheduler->Step();
    scheduler->Step();
    scheduler->Stop();
    for (int i = 0; i < 100 && scheduler->Step(); ++i) {
    }
    EXPECT_TRUE(threw);
}

TEST(TinyFiberFuture, DetachedFutureDoesNotJoin) {
    std::vector<int> order;
    tf::Scheduler::Run([&] {
        {
            auto future = tf::Spawn([&] {
                tf::Yield();
                order.push_back(2);
            });
            future.Detach();
        } // no join here
        order.push_back(1);
    });
    EXPECT_EQ(order, (std::vector<int> {1, 2}));
}

TEST(TinyFiberFuture, SpawnDetachedRuns) {
    int ran = 0;
    tf::Scheduler::Run([&] {
        tf::SpawnDetached([&] {
            tf::Yield();
            ++ran;
        });
    });
    EXPECT_EQ(ran, 1);
}

TEST(TinyFiberFuture, UnstartedFiberNeverRunsBodyAfterStop) {
    bool body_ran = false;
    {
        auto scheduler = tf::Scheduler::Create([&] { tf::SpawnDetached([&] { body_ran = true; }); });
        scheduler->Step(); // entry spawns the child; the child has not started
    } // teardown
    EXPECT_FALSE(body_ran);
}

TEST(TinyFiberFuture, GetFromAnotherSchedulersFiberThrows) {
    std::optional<tf::Future<int>> outer_future;
    bool threw = false;
    tf::Scheduler::Run([&] {
        outer_future.emplace(tf::Spawn([] {
            tf::Yield();
            return 3;
        }));
        auto inner = tf::Scheduler::Create([&] {
            try {
                (void)outer_future->Get();
            } catch (const std::logic_error&) {
                threw = true;
            }
        });
        while (inner->Step()) {
        }
        EXPECT_EQ(outer_future->Get(), 3);
    });
    EXPECT_TRUE(threw);
}
