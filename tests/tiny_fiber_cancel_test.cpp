// Per-fiber cancellation: Future::Cancel() makes the fiber throw
// CancelledError at its next cancellation point (waking it if it is parked in
// one), and a cancelled fiber cancels the children it joins.

#include <cortex/tiny_fiber/tiny_fiber.hpp>

#include <gtest/gtest.h>

#include <chrono>

namespace tf = cortex::tiny_fiber;
using namespace std::chrono_literals;

TEST(TinyFiberCancel, CancelWakesSleepingFiberWithCancelledError) {
    tf::Scheduler::Run([] {
        auto sleeper = tf::Spawn([] {
            tf::SleepFor(1h);
            return 1;
        });
        tf::Yield(); // let it start sleeping
        sleeper.Cancel();
        EXPECT_THROW((void)sleeper.Get(), tf::CancelledError);
    });
}

TEST(TinyFiberCancel, CancelBeforeStartSkipsBody) {
    bool body_ran = false;
    tf::Scheduler::Run([&] {
        auto never = tf::Spawn([&] { body_ran = true; });
        never.Cancel();
        EXPECT_THROW(never.Wait(), tf::CancelledError);
    });
    EXPECT_FALSE(body_ran);
}

TEST(TinyFiberCancel, CancelIsObservedAtCheckPoint) {
    int iterations = 0;
    tf::Scheduler::Run([&] {
        auto worker = tf::Spawn([&] {
            for (;;) {
                ++iterations;
                tf::CheckPoint();
                tf::Yield();
            }
        });
        tf::Yield();
        tf::Yield();
        worker.Cancel();
        EXPECT_THROW(worker.Wait(), tf::CancelledError);
    });
    EXPECT_GE(iterations, 1);
}

TEST(TinyFiberCancel, CancelWakesFutureWaiter) {
    tf::Scheduler::Run([] {
        auto slow = tf::Spawn([] {
            tf::SleepFor(30ms);
            return 1;
        });
        auto waiter = tf::Spawn([&] { return slow.Get(); });
        tf::Yield();
        waiter.Cancel();
        EXPECT_THROW((void)waiter.Get(), tf::CancelledError);
        EXPECT_EQ(slow.Get(), 1); // the fiber it waited for is unaffected
    });
}

TEST(TinyFiberCancel, CancelWakesConditionVariableWaiter) {
    tf::Scheduler::Run([] {
        tf::Mutex mutex;
        tf::ConditionVariable cv;
        auto waiter = tf::Spawn([&] {
            auto guard = tf::Lock(mutex);
            cv.Wait(guard);
        });
        tf::Yield();
        waiter.Cancel();
        EXPECT_THROW(waiter.Wait(), tf::CancelledError);
        EXPECT_FALSE(mutex.IsLocked());
    });
}

TEST(TinyFiberCancel, MutexLockIsNotACancellationPoint) {
    bool acquired = false;
    tf::Scheduler::Run([&] {
        tf::Mutex mutex;
        auto holder = tf::Lock(mutex);
        auto contender = tf::Spawn([&] {
            {
                auto guard = tf::Lock(mutex); // parks: not interrupted by Cancel
                acquired = true;
            }
            tf::Yield(); // cancellation point
        });
        tf::Yield(); // contender parks in Lock()
        contender.Cancel();
        tf::Yield();
        EXPECT_FALSE(acquired);
        {
            auto released = std::move(holder);
        } // unlock: contender acquires
        EXPECT_THROW(contender.Wait(), tf::CancelledError);
    });
    EXPECT_TRUE(acquired);
}

TEST(TinyFiberCancel, IsCancellationRequestedReportsTheFlag) {
    bool before = true;
    bool during = false;
    tf::Scheduler::Run([&] {
        auto worker = tf::Spawn([&] {
            before = tf::IsCancellationRequested();
            try {
                tf::SleepFor(1h);
            } catch (const tf::CancelledError&) {
                during = tf::IsCancellationRequested();
                throw;
            }
        });
        tf::Yield();
        worker.Cancel();
        EXPECT_THROW(worker.Wait(), tf::CancelledError);
    });
    EXPECT_FALSE(before);
    EXPECT_TRUE(during);
    EXPECT_FALSE(tf::IsCancellationRequested()); // plain code: never cancelled
}

TEST(TinyFiberCancel, CancellingParentCancelsChildren) {
    bool child_cancelled = false;
    tf::Scheduler::Run([&] {
        auto parent = tf::Spawn([&] {
            auto child = tf::Spawn([&] {
                try {
                    tf::SleepFor(1h);
                } catch (const tf::CancelledError&) {
                    child_cancelled = true;
                    throw;
                }
            });
            tf::SleepFor(1h);
        });
        tf::Yield();
        tf::Yield();
        parent.Cancel();
        EXPECT_THROW(parent.Wait(), tf::CancelledError);
    });
    EXPECT_TRUE(child_cancelled);
}

TEST(TinyFiberCancel, CancelFinishedFiberIsNoOp) {
    tf::Scheduler::Run([] {
        auto done = tf::Spawn([] { return 5; });
        tf::Yield();
        ASSERT_TRUE(done.IsReady());
        done.Cancel();
        EXPECT_EQ(done.Get(), 5);
    });
}

TEST(TinyFiberCancel, CancelledFiberIsNotAnUnhandledError) {
    EXPECT_NO_THROW(tf::Scheduler::Run([] {
        auto worker = tf::Spawn([] { tf::SleepFor(1h); });
        tf::Yield();
        worker.Cancel();
        worker.Detach();
    }));
}
