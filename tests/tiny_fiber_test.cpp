#include <cortex/tiny_fiber/tiny_fiber.hpp>
#include <gtest/gtest.h>
#include <queue>
#include <string>
#include <vector>

namespace tf = cortex::tiny_fiber;

// ============================================================================
// Scheduler Tests
// ============================================================================

TEST(TinyFiberSchedulerTest, BasicExecution) {
    bool executed = false;
    tf::Scheduler::Run([&executed] {
        executed = true;
    });
    EXPECT_TRUE(executed);
}

TEST(TinyFiberSchedulerTest, SchedulerCurrentThrowsOutsideFiber) {
    EXPECT_THROW(tf::Scheduler::Current(), std::logic_error);
}

TEST(TinyFiberSchedulerTest, SchedulerCurrentWorks) {
    bool checked = false;
    tf::Scheduler::Run([&checked] {
        EXPECT_NO_THROW(tf::Scheduler::Current());
        EXPECT_TRUE(tf::Scheduler::Current().IsRunning());
        checked = true;
    });
    EXPECT_TRUE(checked);
}

// ============================================================================
// Yield Tests
// ============================================================================

TEST(TinyFiberYieldTest, BasicYield) {
    std::vector<int> sequence;
    tf::Scheduler::Run([&sequence] {
        sequence.push_back(1);
        tf::Yield();
        sequence.push_back(2);
    });
    std::vector<int> expected = {1, 2};
    EXPECT_EQ(sequence, expected);
}

TEST(TinyFiberYieldTest, YieldIfOthersReadyReturnsFalseWhenAlone) {
    bool result = true;
    tf::Scheduler::Run([&result] {
        result = tf::YieldIfOthersReady();
    });
    EXPECT_FALSE(result);
}

// ============================================================================
// Spawn and Future Tests
// ============================================================================

TEST(TinyFiberFutureTest, SpawnAndWait) {
    std::vector<int> sequence;
    tf::Scheduler::Run([&sequence] {
        sequence.push_back(1);

        auto future = tf::Spawn([&sequence] {
            sequence.push_back(2);
        });

        future.Wait();
        sequence.push_back(3);
    });
    std::vector<int> expected = {1, 2, 3};
    EXPECT_EQ(sequence, expected);
}

TEST(TinyFiberFutureTest, SpawnWithReturnValue) {
    int result = 0;
    tf::Scheduler::Run([&result] {
        auto future = tf::Spawn([] {
            return 42;
        });
        result = future.Get();
    });
    EXPECT_EQ(result, 42);
}

TEST(TinyFiberFutureTest, SpawnWithYield) {
    std::vector<int> sequence;
    tf::Scheduler::Run([&sequence] {
        sequence.push_back(1);

        auto future = tf::Spawn([&sequence] {
            sequence.push_back(3);
            tf::Yield();
            sequence.push_back(5);
            return 100;
        });

        sequence.push_back(2);
        tf::Yield();
        sequence.push_back(4);

        int result = future.Get();
        sequence.push_back(6);
        EXPECT_EQ(result, 100);
    });
    std::vector<int> expected = {1, 2, 3, 4, 5, 6};
    EXPECT_EQ(sequence, expected);
}

TEST(TinyFiberFutureTest, IsReady) {
    tf::Scheduler::Run([] {
        auto future = tf::Spawn([] {
            return 42;
        });

        // Fiber hasn't run yet after spawn, but will after yield
        tf::Yield();

        // After yield, the spawned fiber should have completed
        EXPECT_TRUE(future.IsReady());
        EXPECT_EQ(future.Get(), 42);
    });
}

TEST(TinyFiberFutureTest, MultipleFibers) {
    std::vector<int> sequence;
    tf::Scheduler::Run([&sequence] {
        auto f1 = tf::Spawn([&sequence] {
            sequence.push_back(1);
            tf::Yield();
            sequence.push_back(4);
        });

        auto f2 = tf::Spawn([&sequence] {
            sequence.push_back(2);
            tf::Yield();
            sequence.push_back(5);
        });

        sequence.push_back(3);
        tf::Yield();
        sequence.push_back(6);

        f1.Wait();
        f2.Wait();
    });

    // Order depends on scheduling, but all should be present
    EXPECT_EQ(sequence.size(), 6);
}

TEST(TinyFiberFutureTest, FutureDestructorWaits) {
    std::vector<int> sequence;
    tf::Scheduler::Run([&sequence] {
        sequence.push_back(1);
        {
            auto future = tf::Spawn([&sequence] {
                sequence.push_back(2);
            });
            // Future destructor should wait
        }
        sequence.push_back(3);
    });
    std::vector<int> expected = {1, 2, 3};
    EXPECT_EQ(sequence, expected);
}

TEST(TinyFiberFutureTest, FutureWithException) {
    tf::Scheduler::Run([] {
        auto future = tf::Spawn([]() -> int {
            throw std::runtime_error("test error");
        });

        EXPECT_THROW(future.Get(), std::runtime_error);
    });
}

TEST(TinyFiberFutureTest, VoidFutureWithException) {
    tf::Scheduler::Run([] {
        auto future = tf::Spawn([] {
            throw std::runtime_error("test error");
        });

        EXPECT_THROW(future.Wait(), std::runtime_error);
    });
}

// ============================================================================
// Mutex Tests
// ============================================================================

TEST(TinyFiberMutexTest, BasicLockUnlock) {
    tf::Scheduler::Run([] {
        tf::Mutex mutex;
        EXPECT_FALSE(mutex.IsLocked());

        mutex.Lock();
        EXPECT_TRUE(mutex.IsLocked());

        mutex.Unlock();
        EXPECT_FALSE(mutex.IsLocked());
    });
}

TEST(TinyFiberMutexTest, TryLock) {
    tf::Scheduler::Run([] {
        tf::Mutex mutex;

        EXPECT_TRUE(mutex.TryLock());
        EXPECT_TRUE(mutex.IsLocked());

        // TryLock should fail when already locked
        // Note: Can't test this in same fiber due to single-threaded nature

        mutex.Unlock();
    });
}

TEST(TinyFiberMutexTest, Guard) {
    tf::Scheduler::Run([] {
        tf::Mutex mutex;

        {
            auto guard = tf::Lock(mutex);
            EXPECT_TRUE(mutex.IsLocked());
        }
        EXPECT_FALSE(mutex.IsLocked());
    });
}

TEST(TinyFiberMutexTest, MutexProtectsResource) {
    int shared_counter = 0;
    tf::Scheduler::Run([&shared_counter] {
        tf::Mutex mutex;

        auto f1 = tf::Spawn([&shared_counter, &mutex] {
            for (int i = 0; i < 10; ++i) {
                auto guard = tf::Lock(mutex);
                int temp = shared_counter;
                tf::Yield(); // Yield while holding lock
                shared_counter = temp + 1;
            }
        });

        auto f2 = tf::Spawn([&shared_counter, &mutex] {
            for (int i = 0; i < 10; ++i) {
                auto guard = tf::Lock(mutex);
                int temp = shared_counter;
                tf::Yield(); // Yield while holding lock
                shared_counter = temp + 1;
            }
        });

        f1.Wait();
        f2.Wait();
    });

    // With proper mutex protection, counter should be exactly 20
    EXPECT_EQ(shared_counter, 20);
}

TEST(TinyFiberMutexTest, RecursiveLockThrows) {
    tf::Scheduler::Run([] {
        tf::Mutex mutex;
        mutex.Lock();
        EXPECT_THROW(mutex.Lock(), std::logic_error);
        mutex.Unlock();
    });
}

// ============================================================================
// ConditionVariable Tests
// ============================================================================

TEST(TinyFiberCondVarTest, BasicNotifyOne) {
    std::vector<int> sequence;
    tf::Scheduler::Run([&sequence] {
        tf::Mutex mutex;
        tf::ConditionVariable cv;
        bool ready = false;

        auto waiter = tf::Spawn([&] {
            auto guard = tf::Lock(mutex);
            sequence.push_back(1);
            cv.Wait(guard, [&ready] {
                return ready;
            });
            sequence.push_back(4);
        });

        // Give waiter a chance to start
        tf::Yield();

        {
            auto guard = tf::Lock(mutex);
            sequence.push_back(2);
            ready = true;
            sequence.push_back(3);
            cv.NotifyOne();
        }

        waiter.Wait();
    });

    std::vector<int> expected = {1, 2, 3, 4};
    EXPECT_EQ(sequence, expected);
}

TEST(TinyFiberCondVarTest, NotifyAll) {
    int woken_count = 0;
    tf::Scheduler::Run([&woken_count] {
        tf::Mutex mutex;
        tf::ConditionVariable cv;
        bool ready = false;

        auto w1 = tf::Spawn([&] {
            auto guard = tf::Lock(mutex);
            cv.Wait(guard, [&ready] {
                return ready;
            });
            woken_count++;
        });

        auto w2 = tf::Spawn([&] {
            auto guard = tf::Lock(mutex);
            cv.Wait(guard, [&ready] {
                return ready;
            });
            woken_count++;
        });

        tf::Yield();
        tf::Yield();

        {
            auto guard = tf::Lock(mutex);
            ready = true;
            cv.NotifyAll();
        }

        w1.Wait();
        w2.Wait();
    });

    EXPECT_EQ(woken_count, 2);
}

TEST(TinyFiberCondVarTest, ProducerConsumer) {
    std::vector<int> consumed;
    tf::Scheduler::Run([&consumed] {
        std::queue<int> buffer;
        tf::Mutex mutex;
        tf::ConditionVariable cv;
        bool done = false;

        // Producer
        auto producer = tf::Spawn([&] {
            for (int i = 1; i <= 5; ++i) {
                {
                    auto guard = tf::Lock(mutex);
                    buffer.push(i);
                    cv.NotifyOne();
                }
                tf::Yield();
            }
            auto guard = tf::Lock(mutex);
            done = true;
            cv.NotifyAll();
        });

        // Consumer
        auto consumer = tf::Spawn([&] {
            while (true) {
                auto guard = tf::Lock(mutex);
                cv.Wait(guard, [&] {
                    return !buffer.empty() || done;
                });

                while (!buffer.empty()) {
                    consumed.push_back(buffer.front());
                    buffer.pop();
                }

                if (done && buffer.empty()) {
                    break;
                }
            }
        });

        producer.Wait();
        consumer.Wait();
    });

    std::vector<int> expected = {1, 2, 3, 4, 5};
    EXPECT_EQ(consumed, expected);
}

// ============================================================================
// Complex Scenarios
// ============================================================================

TEST(TinyFiberComplexTest, ManyFibers) {
    int sum = 0;
    tf::Scheduler::Run([&sum] {
        std::vector<tf::Future<int>> futures;

        for (int i = 0; i < 10; ++i) {
            futures.push_back(tf::Spawn([i] {
                tf::Yield();
                return i * i;
            }));
        }

        for (auto& f : futures) {
            sum += f.Get();
        }
    });

    // Sum of squares 0..9: 0+1+4+9+16+25+36+49+64+81 = 285
    EXPECT_EQ(sum, 285);
}

TEST(TinyFiberComplexTest, NestedSpawn) {
    std::string result;
    tf::Scheduler::Run([&result] {
        result += "A";

        auto outer = tf::Spawn([&result] {
            result += "B";

            auto inner = tf::Spawn([&result] {
                result += "C";
                tf::Yield();
                result += "E";
            });

            result += "D";
            inner.Wait();
            result += "F";
        });

        outer.Wait();
        result += "G";
    });

    // The exact order depends on scheduling, but should complete correctly
    EXPECT_EQ(result.size(), 7);
    EXPECT_NE(result.find("A"), std::string::npos);
    EXPECT_NE(result.find("G"), std::string::npos);
}

TEST(TinyFiberComplexTest, CustomStackSize) {
    bool executed = false;
    tf::Scheduler::Run([&executed] {
        auto future = tf::Spawn(
            [&executed] {
                executed = true;
            },
            1024 * 64); // 64KB stack

        future.Wait();
    });
    EXPECT_TRUE(executed);
}

TEST(TinyFiberComplexTest, SchedulerConfig) {
    bool executed = false;
    tf::Scheduler::Config config;
    config.default_stack_size = 1024 * 128; // 128KB

    tf::Scheduler::Run(
        [&executed] {
            EXPECT_EQ(tf::Scheduler::Current().GetDefaultStackSize(), 1024 * 128);
            executed = true;
        },
        config);

    EXPECT_TRUE(executed);
}
