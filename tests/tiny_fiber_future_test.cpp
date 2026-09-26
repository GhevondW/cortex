// Future semantics: where Get()/Wait() may be called from, what a Future
// holds when its fiber could not finish, and detached / fire-and-forget
// fibers.

#include <cortex/tiny_fiber/tiny_fiber.hpp>

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <future>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
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

// Structured shutdown: even while the scheduler stops, a parent's scope does
// not end before the children it spawned have finished — children may use the
// parent's locals (a Mutex, captured references) until they exit.
TEST(TinyFiberFuture, StopStillJoinsChildrenBeforeParentScopeEnds) {
    std::vector<std::string> events;
    auto scheduler = tf::Scheduler::Create([&] {
        struct Scope {
            std::vector<std::string>& events;
            ~Scope() {
                events.push_back("parent scope ended");
            }
        } scope {events};
        auto child = tf::Spawn([&] {
            try {
                for (;;) {
                    tf::Yield();
                }
            } catch (const tf::SchedulerStoppingError&) {
                events.push_back("child stopped");
                throw;
            }
        });
        for (;;) {
            tf::Yield();
        }
    });
    scheduler->Step();
    scheduler->Step();
    scheduler->Stop();
    while (!scheduler->IsDone()) {
        scheduler->Step();
    }
    EXPECT_EQ(events, (std::vector<std::string> {"child stopped", "parent scope ended"}));
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

    EXPECT_EQ(scheduler->RunFor(1s), tf::Scheduler::Status::kWaiting); // waits on the outside world
    EXPECT_EQ(wakeups, 0);

    promise->SetValue(42); // e.g. from a JavaScript callback
    EXPECT_EQ(wakeups, 1);
    EXPECT_EQ(scheduler->GetStatus(), tf::Scheduler::Status::kRunnable);
    EXPECT_EQ(scheduler->RunFor(1s), tf::Scheduler::Status::kDone);
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
    scheduler->RunFor(1s);
    promise.reset();
    scheduler->RunFor(1s);
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
    EXPECT_EQ(scheduler->RunFor(1s), tf::Scheduler::Status::kDone);
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
    EXPECT_EQ(scheduler->RunFor(1s), tf::Scheduler::Status::kDone);
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

// Once Post() has queued its work, the scheduler thread may run it, finish
// and destroy the scheduler at any moment. Post() must not touch the
// scheduler after that point: the wake-up handler it calls may outlive it.
// (Under ASan the old code read the destroyed handler here.)
TEST(TinyFiberPost, PostDoesNotTouchTheSchedulerAfterQueueingWork) {
    std::promise<void> destroyed;
    std::shared_future<void> destroyed_signal = destroyed.get_future().share();
    int handler_saw = 0;

    std::thread scheduler_thread([&] {
        bool done = false;
        auto scheduler = tf::Scheduler::Create([] {
        });
        while (scheduler->Step()) {
        }
        int canary = 1234; // not const: it must live in the handler's storage
        scheduler->SetWakeupHandler([canary, destroyed_signal, &handler_saw] {
            // Keep Post() inside its call to the handler until the scheduler
            // is gone, then use the handler's own captured state.
            destroyed_signal.wait_for(std::chrono::seconds(5));
            handler_saw = canary;
        });
        std::thread poster([&scheduler, &done] {
            scheduler->Post([&done] {
                done = true;
            });
        });
        while (!done) {
            scheduler->RunFor(1ms);
        }
        scheduler.reset();
        destroyed.set_value();
        poster.join();
    });
    scheduler_thread.join();
    EXPECT_EQ(handler_saw, 1234);
}
#endif

TEST(TinyFiberWait, WaitAllAcceptsEmptyFutures) {
    tf::Scheduler::Run([] {
        auto detached = tf::Spawn([] {
            tf::Yield();
        });
        detached.Detach(); // empty now: IsReady() is true
        auto normal = tf::Spawn([] {
            return 1;
        });
        tf::WaitAll(detached, normal);
        EXPECT_EQ(normal.Get(), 1);
    });
}

// The wake-up handler is for work that arrives while nobody steps the
// scheduler; a timer firing inside RunFor() must not call it.
TEST(TinyFiberPromise, TimerFiringInsideRunForDoesNotCallWakeupHandler) {
    int wakeups = 0;
    auto scheduler = tf::Scheduler::Create(
        [] {
            tf::SleepFor(10ms);
        },
        FakeClockConfig());
    scheduler->SetWakeupHandler([&] {
        ++wakeups;
    });
    EXPECT_EQ(scheduler->RunFor(1ms), tf::Scheduler::Status::kWaiting);
    g_now += 10ms;
    EXPECT_EQ(scheduler->RunFor(1ms), tf::Scheduler::Status::kDone);
    EXPECT_EQ(wakeups, 0);
}

namespace {

// Records whether it was destroyed while a fiber was running.
struct FiberContextProbe {
    explicit FiberContextProbe(bool* in_fiber)
        : in_fiber_(in_fiber) {}
    FiberContextProbe(FiberContextProbe&& other) noexcept
        : in_fiber_(std::exchange(other.in_fiber_, nullptr)) {}
    FiberContextProbe(const FiberContextProbe&) = delete;
    FiberContextProbe& operator=(const FiberContextProbe&) = delete;
    FiberContextProbe& operator=(FiberContextProbe&&) = delete;
    ~FiberContextProbe() {
        if (in_fiber_ != nullptr) {
            tf::Scheduler* scheduler = tf::Scheduler::TryCurrent();
            *in_fiber_ = scheduler != nullptr && scheduler->GetCurrentFiber() != nullptr;
        }
    }

private:
    bool* in_fiber_;
};

} // namespace

// What a fiber captured dies with the fiber, inside it: destructors may use
// the scheduler (unlock a Mutex, break a Promise, join a Future).
TEST(TinyFiberFuture, CapturesAreDestroyedInsideTheFiber) {
    bool spawned_in_fiber = false;
    bool detached_in_fiber = false;
    tf::Scheduler::Run([&] {
        auto future = tf::Spawn([probe = FiberContextProbe(&spawned_in_fiber)] {
        });
        tf::SpawnDetached([probe = FiberContextProbe(&detached_in_fiber)] {
        });
        future.Wait();
    });
    EXPECT_TRUE(spawned_in_fiber);
    EXPECT_TRUE(detached_in_fiber);
}

// A promise owned by a fiber that finished without fulfilling it breaks in
// the same step, so a driver never sees kWaiting with nothing to wait for.
TEST(TinyFiberPromise, CapturedPromiseBreaksWhenItsFiberFinishes) {
    bool broken = false;
    auto scheduler = tf::Scheduler::Create([&broken] {
        tf::Promise<int> promise;
        auto future = promise.GetFuture();
        tf::SpawnDetached([promise = std::move(promise)]() mutable {
        });
        try {
            future.Get();
        } catch (const tf::BrokenPromiseError&) {
            broken = true;
        }
    });
    EXPECT_EQ(scheduler->RunFor(1s), tf::Scheduler::Status::kDone);
    EXPECT_TRUE(broken);
}

// Run() must not wait forever for a promise whose cancelled producer owned it.
TEST(TinyFiberPromise, CancelledProducerBreaksItsPromise) {
    EXPECT_THROW(tf::Scheduler::Run([] {
                     tf::Promise<int> promise;
                     auto future = promise.GetFuture();
                     auto producer = tf::Spawn([promise = std::move(promise)]() mutable {
                         tf::SleepFor(10s);
                         promise.SetValue(1);
                     });
                     producer.Cancel();
                     return future.Get();
                 }),
                 tf::BrokenPromiseError);
}

// Containers of futures work directly, not only std::span.
TEST(TinyFiberWait, WaitAllAndWaitAnyTakeContainers) {
    tf::Scheduler::Run([] {
        std::vector<tf::Future<int>> futures;
        for (int i = 0; i < 3; ++i) {
            futures.push_back(tf::Spawn([i] {
                tf::SleepFor(std::chrono::milliseconds(1 + i * 5));
                return i;
            }));
        }
        EXPECT_EQ(tf::WaitAny(futures), 0u);
        tf::WaitAll(futures);
        for (auto& future : futures) {
            EXPECT_TRUE(future.IsReady());
        }

        std::array<tf::Future<void>, 2> pair {tf::Spawn([] {
                                              }),
                                              tf::Spawn([] {
                                              })};
        tf::WaitAll(pair);
        EXPECT_TRUE(pair[0].IsReady() && pair[1].IsReady());
    });
}
