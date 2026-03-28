#include <cortex/fiber/fiber.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <future>
#include <gtest/gtest.h>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <vector>

namespace cf = cortex::fiber;

TEST(FiberSchedulerTest, BasicExecution) {
    bool executed = false;
    cf::Scheduler::Run([&executed] {
        executed = true;
    });
    EXPECT_TRUE(executed);
}

TEST(FiberSchedulerTest, CurrentThrowsOutsideFiber) {
    EXPECT_THROW(cf::Scheduler::Current(), std::logic_error);
}

TEST(FiberSchedulerTest, CurrentInsideFiber) {
    bool checked = false;
    cf::Scheduler::Run([&checked] {
        EXPECT_NO_THROW(cf::Scheduler::Current());
        checked = true;
    });
    EXPECT_TRUE(checked);
}

TEST(FiberFutureTest, SpawnAndGet) {
    int value = 0;
    cf::Scheduler::Run([&value] {
        auto future = cf::Spawn([] {
            cf::Yield();
            return 42;
        });
        value = future.Get();
    });
    EXPECT_EQ(value, 42);
}

TEST(FiberFutureTest, FutureWaitFromExternalThread) {
    std::promise<cf::Future<int>> promise;
    auto std_future = promise.get_future();

    auto scheduler = cf::Scheduler::Create([&promise] {
        promise.set_value(cf::Spawn([] {
            for (int i = 0; i < 32; ++i) {
                cf::Yield();
            }
            return 77;
        }));
    });

    auto future = std_future.get();
    EXPECT_EQ(future.Get(), 77);
    scheduler->Wait();
}

TEST(FiberFutureTest, ExceptionPropagation) {
    cf::Scheduler::Run([] {
        auto future = cf::Spawn([]() -> int {
            throw std::runtime_error("fiber failure");
        });

        EXPECT_THROW(future.Get(), std::runtime_error);
    });
}

TEST(FiberFutureTest, VoidFutureExceptionPropagation) {
    cf::Scheduler::Run([] {
        auto future = cf::Spawn([] {
            throw std::runtime_error("void fiber failure");
        });

        EXPECT_THROW(future.Wait(), std::runtime_error);
    });
}

TEST(FiberFutureTest, GetCalledTwiceThrows) {
    cf::Scheduler::Run([] {
        auto future = cf::Spawn([] {
            return 12;
        });

        EXPECT_EQ(future.Get(), 12);
        EXPECT_THROW(future.Get(), std::logic_error);
    });
}

TEST(FiberFutureTest, VoidWaitCalledTwiceThrows) {
    cf::Scheduler::Run([] {
        auto future = cf::Spawn([] {});
        future.Wait();
        EXPECT_THROW(future.Wait(), std::logic_error);
    });
}

TEST(FiberFutureTest, MoveFuture) {
    int result = 0;
    cf::Scheduler::Run([&result] {
        auto f1 = cf::Spawn([] {
            return 314;
        });
        auto f2 = std::move(f1);
        result = f2.Get();
    });
    EXPECT_EQ(result, 314);
}

TEST(FiberFutureTest, IsReadyTransition) {
    cf::Scheduler::Run([] {
        auto future = cf::Spawn([] {
            cf::Yield();
            return 99;
        });

        EXPECT_FALSE(future.IsReady());
        while (!future.IsReady()) {
            cf::Yield();
        }
        EXPECT_EQ(future.Get(), 99);
    });
}

TEST(FiberSchedulerTest, ExecutesAcrossMultipleWorkers) {
    std::mutex ids_mutex;
    std::unordered_set<std::thread::id> thread_ids;

    constexpr int task_count = 128;
    std::atomic<int> started {0};
    std::atomic<bool> release {false};

    cf::Scheduler::Config config;
    config.worker_threads = 4;
    config.enable_work_stealing = true;

    cf::Scheduler::Run(
        [&] {
            std::vector<cf::Future<void>> futures;
            futures.reserve(static_cast<std::size_t>(task_count));

            for (int i = 0; i < task_count; ++i) {
                futures.push_back(cf::Spawn([&] {
                    {
                        std::lock_guard lock(ids_mutex);
                        thread_ids.insert(std::this_thread::get_id());
                    }
                    started.fetch_add(1, std::memory_order_acq_rel);
                    while (!release.load(std::memory_order_acquire)) {
                        cf::Yield();
                    }
                }));
            }

            while (started.load(std::memory_order_acquire) < task_count) {
                cf::Yield();
            }

            release.store(true, std::memory_order_release);
            for (auto& future : futures) {
                future.Wait();
            }
        },
        config);

    EXPECT_GE(thread_ids.size(), static_cast<std::size_t>(2));
}

TEST(FiberMutexTest, ProtectsSharedCounter) {
    std::int64_t counter = 0;

    constexpr int fibers_count = 16;
    constexpr int increments = 500;

    cf::Scheduler::Config config;
    config.worker_threads = 4;

    cf::Scheduler::Run(
        [&] {
            cf::Mutex mutex;
            std::vector<cf::Future<void>> futures;
            futures.reserve(static_cast<std::size_t>(fibers_count));

            for (int i = 0; i < fibers_count; ++i) {
                futures.push_back(cf::Spawn([&] {
                    for (int j = 0; j < increments; ++j) {
                        auto guard = cf::Lock(mutex);
                        ++counter;
                    }
                }));
            }

            for (auto& future : futures) {
                future.Wait();
            }
        },
        config);

    EXPECT_EQ(counter, static_cast<std::int64_t>(fibers_count * increments));
}

TEST(FiberMutexTest, RecursiveLockThrows) {
    cf::Scheduler::Run([] {
        cf::Mutex mutex;
        auto guard = cf::Lock(mutex);
        EXPECT_THROW(mutex.Lock(), std::logic_error);
    });
}

TEST(FiberMutexTest, UnlockUnlockedThrows) {
    cf::Scheduler::Run([] {
        cf::Mutex mutex;
        EXPECT_THROW(mutex.Unlock(), std::logic_error);
    });
}

TEST(FiberMutexTest, UnlockByNonOwnerThrows) {
    std::atomic<bool> ready {false};
    std::atomic<bool> done {false};
    std::atomic<bool> bad_unlock_thrown {false};

    cf::Scheduler::Run([&] {
        cf::Mutex mutex;

        auto owner = cf::Spawn([&] {
            mutex.Lock();
            ready.store(true, std::memory_order_release);
            while (!done.load(std::memory_order_acquire)) {
                cf::Yield();
            }
            mutex.Unlock();
        });

        auto intruder = cf::Spawn([&] {
            while (!ready.load(std::memory_order_acquire)) {
                cf::Yield();
            }
            try {
                mutex.Unlock();
            } catch (const std::logic_error&) {
                bad_unlock_thrown.store(true, std::memory_order_release);
            }
            done.store(true, std::memory_order_release);
        });

        owner.Wait();
        intruder.Wait();
    });

    EXPECT_TRUE(bad_unlock_thrown.load(std::memory_order_acquire));
}

TEST(FiberConditionVariableTest, ProducerConsumer) {
    std::deque<int> queue;
    std::vector<int> consumed;
    bool done = false;

    cf::Scheduler::Config config;
    config.worker_threads = 4;

    cf::Scheduler::Run(
        [&] {
            cf::Mutex mutex;
            cf::ConditionVariable cv;

            auto producer = cf::Spawn([&] {
                for (int i = 1; i <= 100; ++i) {
                    auto guard = cf::Lock(mutex);
                    queue.push_back(i);
                    cv.NotifyOne();
                }

                auto guard = cf::Lock(mutex);
                done = true;
                cv.NotifyAll();
            });

            auto consumer = cf::Spawn([&] {
                while (true) {
                    auto guard = cf::Lock(mutex);
                    cv.Wait(guard, [&] {
                        return !queue.empty() || done;
                    });

                    if (queue.empty() && done) {
                        break;
                    }

                    consumed.push_back(queue.front());
                    queue.pop_front();
                }
            });

            producer.Wait();
            consumer.Wait();
        },
        config);

    EXPECT_EQ(consumed.size(), static_cast<std::size_t>(100));
    EXPECT_EQ(consumed.front(), 1);
    EXPECT_EQ(consumed.back(), 100);
}

TEST(FiberConditionVariableTest, NotifyOneWakesExactlyOnePerSignal) {
    constexpr int waiter_count = 4;
    std::atomic<int> consumed_tokens {0};

    cf::Scheduler::Config config;
    config.worker_threads = 4;

    cf::Scheduler::Run(
        [&] {
            cf::Mutex mutex;
            cf::ConditionVariable cv;
            int tokens = 0;

            std::vector<cf::Future<void>> waiters;
            waiters.reserve(waiter_count);

            for (int i = 0; i < waiter_count; ++i) {
                waiters.push_back(cf::Spawn([&] {
                    auto guard = cf::Lock(mutex);
                    cv.Wait(guard, [&] {
                        return tokens > 0;
                    });
                    --tokens;
                    consumed_tokens.fetch_add(1, std::memory_order_acq_rel);
                }));
            }

            for (int i = 0; i < waiter_count; ++i) {
                {
                    auto guard = cf::Lock(mutex);
                    ++tokens;
                    cv.NotifyOne();
                }
                while (consumed_tokens.load(std::memory_order_acquire) < (i + 1)) {
                    cf::Yield();
                }
            }

            for (auto& waiter : waiters) {
                waiter.Wait();
            }
        },
        config);

    EXPECT_EQ(consumed_tokens.load(std::memory_order_acquire), waiter_count);
}

TEST(FiberSchedulerTest, YieldIfOthersReadyBehavior) {
    std::atomic<bool> yielded_with_peer {false};
    bool yielded_alone = true;

    cf::Scheduler::Run([&] {
        yielded_alone = cf::YieldIfOthersReady();

        auto peer = cf::Spawn([&] {
            cf::Yield();
        });

        yielded_with_peer.store(cf::YieldIfOthersReady(), std::memory_order_release);
        peer.Wait();
    });

    EXPECT_FALSE(yielded_alone);
    EXPECT_TRUE(yielded_with_peer.load(std::memory_order_acquire));
}

TEST(FiberSchedulerTest, StopUnblocksConditionVariableWaiters) {
    std::atomic<bool> waiter_started {false};
    std::atomic<bool> waiter_stopped {false};

    auto scheduler = cf::Scheduler::Create([&] {
        cf::Mutex mutex;
        cf::ConditionVariable cv;

        auto waiter = cf::Spawn([&] {
            try {
                auto guard = cf::Lock(mutex);
                waiter_started.store(true, std::memory_order_release);
                cv.Wait(guard);
            } catch (const cf::SchedulerStoppingError&) {
                waiter_stopped.store(true, std::memory_order_release);
            }
        });

        while (!waiter_stopped.load(std::memory_order_acquire)) {
            cf::Yield();
        }
        waiter.Wait();
    });

    for (int i = 0; i < 1000 && !waiter_started.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_TRUE(waiter_started.load(std::memory_order_acquire));

    scheduler->Stop();
    scheduler->Wait();

    EXPECT_TRUE(waiter_stopped.load(std::memory_order_acquire));
}

TEST(FiberSchedulerTest, ConfigValuesAreApplied) {
    cf::Scheduler::Config config;
    config.worker_threads = 3;
    config.enable_work_stealing = false;
    config.default_stack_size = 128 * 1024;

    auto scheduler = cf::Scheduler::Create([] {}, config);
    EXPECT_EQ(scheduler->GetWorkerCount(), static_cast<std::size_t>(3));
    EXPECT_FALSE(scheduler->IsWorkStealingEnabled());
    EXPECT_EQ(scheduler->GetDefaultStackSize(), static_cast<std::size_t>(128 * 1024));
    scheduler->Wait();
}

TEST(FiberSchedulerTest, StressManyFibersWithoutStealing) {
    constexpr int tasks = 500;
    std::atomic<int> completed {0};

    cf::Scheduler::Config config;
    config.worker_threads = 4;
    config.enable_work_stealing = false;

    cf::Scheduler::Run(
        [&] {
            std::vector<cf::Future<void>> futures;
            futures.reserve(tasks);

            for (int i = 0; i < tasks; ++i) {
                futures.push_back(cf::Spawn([&] {
                    cf::Yield();
                    completed.fetch_add(1, std::memory_order_acq_rel);
                }));
            }

            for (auto& f : futures) {
                f.Wait();
            }
        },
        config);

    EXPECT_EQ(completed.load(std::memory_order_acquire), tasks);
}
