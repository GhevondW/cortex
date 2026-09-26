// Future semantics: where Get()/Wait() may be called from, what a Future
// holds when its fiber could not finish, and detached / fire-and-forget
// fibers.

#include <cortex/tiny_fiber/tiny_fiber.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace tf = cortex::tiny_fiber;
using namespace std::chrono_literals;

namespace {

tf::Scheduler::TimePoint g_now {};

tf::Scheduler::TimePoint FakeNow() {
    return g_now;
}

tf::Scheduler::Config FakeClockConfig() {
    g_now = tf::Scheduler::TimePoint {} + 1h;
    tf::Scheduler::Config config;
    config.clock = &FakeNow;
    return config;
}

} // namespace

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
    auto scheduler = tf::Scheduler::Create([&] {
        escaped.emplace(tf::Spawn([] {
            tf::Yield();
        }));
    });
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
        auto scheduler = tf::Scheduler::Create([&] {
            tf::SpawnDetached([&] {
                body_ran = true;
            });
        });
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

// --- Timed waits, WaitAll / WaitAny ----------------------------------------

TEST(TinyFiberWait, WaitAnyReturnsFirstReadyIndex) {
    std::size_t first = 99;
    int slow_value = 0;
    int fast_value = 0;
    auto scheduler = tf::Scheduler::Create(
        [&] {
            auto slow = tf::Spawn([] {
                tf::SleepFor(30ms);
                return 1;
            });
            auto fast = tf::Spawn([] {
                tf::SleepFor(5ms);
                return 2;
            });
            first = tf::WaitAny(slow, fast);
            fast_value = fast.Get();
            slow_value = slow.Get();
        },
        FakeClockConfig());
    scheduler->RunFor(1ms);
    g_now += 5ms;
    scheduler->RunFor(1ms);
    EXPECT_EQ(first, 1u);
    EXPECT_EQ(fast_value, 2);
    g_now += 25ms;
    EXPECT_EQ(scheduler->RunFor(1ms), tf::Scheduler::Status::kDone);
    EXPECT_EQ(slow_value, 1);
}

TEST(TinyFiberWait, WaitAnyReturnsImmediatelyWhenOneIsReady) {
    tf::Scheduler::Run([] {
        auto never = tf::Spawn([] {
            tf::SleepFor(1h);
        });
        auto done = tf::Spawn([] {
            return 3;
        });
        tf::Yield();
        EXPECT_EQ(tf::WaitAny(never, done), 1u);
        never.Cancel();
        EXPECT_THROW(never.Wait(), tf::CancelledError);
    });
}

TEST(TinyFiberWait, WaitAllWaitsForEvery) {
    tf::Scheduler::Run([] {
        std::vector<tf::Future<int>> futures;
        for (int i = 0; i < 5; ++i) {
            futures.push_back(tf::Spawn([i] {
                for (int y = 0; y < i; ++y) {
                    tf::Yield();
                }
                return i;
            }));
        }
        tf::WaitAll(std::span(futures));
        for (int i = 0; i < 5; ++i) {
            ASSERT_TRUE(futures[static_cast<std::size_t>(i)].IsReady());
            EXPECT_EQ(futures[static_cast<std::size_t>(i)].Get(), i);
        }
    });
}

TEST(TinyFiberWait, WaitAnyOverSpan) {
    tf::Scheduler::Run([] {
        std::vector<tf::Future<int>> futures;
        futures.push_back(tf::Spawn([] {
            tf::SleepFor(1h);
            return 0;
        }));
        futures.push_back(tf::Spawn([] {
            tf::Yield();
            return 1;
        }));
        EXPECT_EQ(tf::WaitAny(std::span(futures)), 1u);
        futures[0].Cancel();
    });
}

TEST(TinyFiberWait, FutureWaitForTimesOut) {
    bool first_wait = true;
    bool second_wait = false;
    auto scheduler = tf::Scheduler::Create(
        [&] {
            auto slow = tf::Spawn([] {
                tf::SleepFor(20ms);
            });
            first_wait = slow.WaitFor(10ms);
            second_wait = slow.WaitFor(20ms);
        },
        FakeClockConfig());
    scheduler->RunFor(1ms);
    g_now += 10ms;
    scheduler->RunFor(1ms); // the 10 ms wait times out
    EXPECT_FALSE(first_wait);
    g_now += 10ms;
    scheduler->RunFor(1ms); // the producer finishes inside the second wait
    EXPECT_TRUE(second_wait);
    EXPECT_TRUE(scheduler->IsDone());
}

TEST(TinyFiberWait, CvWaitForReportsTimeoutAndNotification) {
    bool timed_out_result = true;
    bool notified_result = false;
    auto scheduler = tf::Scheduler::Create(
        [&] {
            tf::Mutex mutex;
            tf::ConditionVariable cv;
            {
                auto guard = tf::Lock(mutex);
                timed_out_result = cv.WaitFor(guard, 5ms);
                EXPECT_TRUE(mutex.IsLocked()); // re-locked after a timeout
            }
            auto notifier = tf::Spawn([&] {
                tf::SleepFor(1ms);
                auto guard = tf::Lock(mutex);
                cv.NotifyOne();
            });
            auto guard = tf::Lock(mutex);
            notified_result = cv.WaitFor(guard, 1h);
        },
        FakeClockConfig());
    scheduler->RunFor(1ms);
    g_now += 5ms;
    scheduler->RunFor(1ms);
    g_now += 1ms;
    scheduler->RunFor(1ms);
    EXPECT_FALSE(timed_out_result);
    EXPECT_TRUE(notified_result);
    EXPECT_TRUE(scheduler->IsDone());
}

TEST(TinyFiberWait, CvWaitForWithPredicate) {
    tf::Scheduler::Run([] {
        tf::Mutex mutex;
        tf::ConditionVariable cv;
        bool flag = false;
        auto setter = tf::Spawn([&] {
            tf::Yield();
            auto guard = tf::Lock(mutex);
            flag = true;
            cv.NotifyAll();
        });
        auto guard = tf::Lock(mutex);
        EXPECT_TRUE(cv.WaitFor(guard, 1h, [&] {
            return flag;
        }));
    });
}

// A waiter that timed out of a condition variable leaves a stale entry in the
// CV's queue. When it later waits on something else, notifying the CV must
// not wake it.
TEST(TinyFiberWait, CvWaitForTimeoutLeavesNoStaleWake) {
    std::string after_notify;
    auto scheduler = tf::Scheduler::Create(
        [&] {
            tf::Mutex mutex;
            tf::ConditionVariable cv;
            auto producer = tf::Spawn([] {
                tf::SleepFor(1h);
                return 1;
            });
            auto waiter = tf::Spawn([&] {
                tf::SetFiberName("waiter");
                {
                    auto guard = tf::Lock(mutex);
                    (void)cv.WaitFor(guard, 5ms); // times out: stale entry stays in cv
                }
                (void)producer.Get(); // now parked on something unrelated
            });
            tf::SleepFor(10ms);
            {
                auto guard = tf::Lock(mutex);
                cv.NotifyOne(); // must skip the stale entry
            }
            after_notify = tf::Scheduler::Current().DescribeFibers();
            producer.Cancel();
            try {
                waiter.Wait();
            } catch (const tf::CancelledError&) {
            }
        },
        FakeClockConfig());
    scheduler->RunFor(1ms);
    g_now += 5ms;
    scheduler->RunFor(1ms);
    g_now += 5ms;
    scheduler->RunFor(1ms);
    EXPECT_NE(after_notify.find("\"waiter\" suspended in Future::Wait"), std::string::npos) << after_notify;
    while (scheduler->Step()) {
    }
}

TEST(TinyFiberWait, WaitAnyInLoopDoesNotGrowWithoutBound) {
    tf::Scheduler::Run([] {
        auto long_lived = tf::Spawn([] {
            tf::SleepFor(1h);
        });
        for (int i = 0; i < 10000; ++i) {
            auto tick = tf::Spawn([] {
                tf::Yield();
            });
            EXPECT_EQ(tf::WaitAny(long_lived, tick), 1u);
        }
        EXPECT_LT(long_lived.StateInternal()->waiters.Size(), 32u);
        long_lived.Cancel();
    });
}

TEST(TinyFiberWait, CancelDuringWaitAnyWakesOnce) {
    tf::Scheduler::Run([] {
        auto a = tf::Spawn([] {
            tf::SleepFor(10ms);
        });
        auto b = tf::Spawn([] {
            tf::SleepFor(20ms);
        });
        auto waiter = tf::Spawn([&] {
            return tf::WaitAny(a, b);
        });
        tf::Yield();
        waiter.Cancel();
        EXPECT_THROW((void)waiter.Get(), tf::CancelledError);
        a.Wait(); // completing both later wakes nobody stale
        b.Wait();
    });
}

// --- Promise, Post, wake-up handler ------------------------------------------

TEST(TinyFiberPromise, FulfilledFromPlainCodeWakesFiber) {
    std::optional<tf::Promise<int>> promise;
    int result = 0;
    int wakeups = 0;
    auto scheduler = tf::Scheduler::Create([&] {
        promise.emplace();
        auto future = promise->GetFuture();
        result = future.Get();
    });
    scheduler->SetWakeupHandler([&] {
        ++wakeups;
    });

    EXPECT_EQ(scheduler->RunFor(1ms), tf::Scheduler::Status::kWaiting); // waits on the outside world
    EXPECT_EQ(wakeups, 0);

    promise->SetValue(42); // e.g. from a JavaScript callback
    EXPECT_EQ(wakeups, 1);
    EXPECT_EQ(scheduler->GetStatus(), tf::Scheduler::Status::kRunnable);
    EXPECT_EQ(scheduler->RunFor(1ms), tf::Scheduler::Status::kDone);
    EXPECT_EQ(result, 42);
}

TEST(TinyFiberPromise, DestroyedPromiseBreaksFuture) {
    std::optional<tf::Promise<int>> promise;
    bool broken = false;
    auto scheduler = tf::Scheduler::Create([&] {
        promise.emplace();
        auto future = promise->GetFuture();
        try {
            (void)future.Get();
        } catch (const tf::BrokenPromiseError&) {
            broken = true;
        }
    });
    scheduler->RunFor(1ms);
    promise.reset();
    scheduler->RunFor(1ms);
    EXPECT_TRUE(broken);
}

TEST(TinyFiberPromise, SetValueTwiceAndGetFutureTwiceThrow) {
    tf::Scheduler::Run([] {
        tf::Promise<int> promise;
        auto future = promise.GetFuture();
        EXPECT_THROW((void)promise.GetFuture(), std::logic_error);
        promise.SetValue(1);
        EXPECT_TRUE(promise.IsFulfilled());
        EXPECT_THROW(promise.SetValue(2), std::logic_error);
        EXPECT_THROW(promise.SetException(std::make_exception_ptr(std::runtime_error("x"))), std::logic_error);
        EXPECT_EQ(future.Get(), 1);
    });
}

TEST(TinyFiberPromise, ExceptionIsDelivered) {
    tf::Scheduler::Run([] {
        tf::Promise<void> promise;
        auto future = promise.GetFuture();
        auto fulfiller = tf::Spawn([&] {
            promise.SetException(std::make_exception_ptr(std::runtime_error("no")));
        });
        EXPECT_THROW(future.Get(), std::runtime_error);
    });
}

TEST(TinyFiberPromise, FiberToFiberDoesNotCallWakeupHandler) {
    int wakeups = 0;
    auto scheduler = tf::Scheduler::Create([&] {
        tf::Promise<void> promise;
        auto future = promise.GetFuture();
        auto fulfiller = tf::Spawn([&] {
            tf::Yield();
            promise.SetValue();
        });
        future.Wait();
    });
    scheduler->SetWakeupHandler([&] {
        ++wakeups;
    });
    EXPECT_EQ(scheduler->RunFor(10ms), tf::Scheduler::Status::kDone);
    EXPECT_EQ(wakeups, 0); // woken inside a step: the driver is already running
}

TEST(TinyFiberPromise, FulfilledAfterSchedulerDestroyedIsSafe) {
    std::optional<tf::Promise<int>> promise;
    std::optional<tf::Future<int>> future;
    {
        auto scheduler = tf::Scheduler::Create([] {
        });
        promise.emplace(*scheduler);
        future.emplace(promise->GetFuture());
        while (scheduler->Step()) {
        }
    }
    promise->SetValue(5); // must not touch the destroyed scheduler
    ASSERT_TRUE(future->IsReady());
    EXPECT_EQ(future->Get(), 5);
}

TEST(TinyFiberPost, PostedWorkRunsOnNextStepAndMaySpawn) {
    bool ran = false;
    auto scheduler = tf::Scheduler::Create([] {
    });
    while (scheduler->Step()) {
    }
    EXPECT_EQ(scheduler->GetStatus(), tf::Scheduler::Status::kDone);
    scheduler->Post([&] {
        tf::SpawnDetached([&] {
            ran = true;
        });
    });
    EXPECT_EQ(scheduler->GetStatus(), tf::Scheduler::Status::kRunnable);
    EXPECT_EQ(scheduler->RunFor(1ms), tf::Scheduler::Status::kDone);
    EXPECT_TRUE(ran);
}

#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
TEST(TinyFiberPost, PostFromAnotherThreadCompletesPromiseWhileRunBlocks) {
    const int value = tf::Scheduler::Run([] {
        auto& scheduler = tf::Scheduler::Current();
        auto promise = std::make_shared<tf::Promise<int>>();
        auto future = promise->GetFuture();
        std::thread worker([&scheduler, promise] {
            std::this_thread::sleep_for(5ms);
            scheduler.Post([promise] {
                promise->SetValue(7);
            });
        });
        const int result = future.Get(); // Run() blocks the thread until Post
        worker.join();
        return result;
    });
    EXPECT_EQ(value, 7);
}
#endif
