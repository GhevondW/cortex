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

// ============================================================================
// Step-based Scheduler Tests (for WASM integration)
// ============================================================================

TEST(TinyFiberStepTest, BasicStep) {
    std::vector<int> sequence;

    auto scheduler = tf::Scheduler::Create([&sequence] {
        sequence.push_back(1);
        tf::Yield();
        sequence.push_back(2);
        tf::Yield();
        sequence.push_back(3);
    });

    EXPECT_FALSE(scheduler.IsDone());

    // Step through
    while (scheduler.Step()) {
        // Each step runs until yield
    }

    EXPECT_TRUE(scheduler.IsDone());
    std::vector<int> expected = {1, 2, 3};
    EXPECT_EQ(sequence, expected);
}

TEST(TinyFiberStepTest, MultipleFibers) {
    int counter = 0;

    auto scheduler = tf::Scheduler::Create([&counter] {
        auto f1 = tf::Spawn([&counter] {
            counter++;
            tf::Yield();
            counter++;
        });

        auto f2 = tf::Spawn([&counter] {
            counter++;
            tf::Yield();
            counter++;
        });

        f1.Wait();
        f2.Wait();
    });

    int steps = 0;
    while (!scheduler.IsDone()) {
        scheduler.Step();
        steps++;
    }

    EXPECT_EQ(counter, 4);
    EXPECT_GT(steps, 1); // Should take multiple steps
}

TEST(TinyFiberStepTest, StepWithMutex) {
    int value = 0;

    auto scheduler = tf::Scheduler::Create([&value] {
        tf::Mutex mutex;

        auto f1 = tf::Spawn([&value, &mutex] {
            auto guard = tf::Lock(mutex);
            value = 1;
            tf::Yield();
            value = 2;
        });

        auto f2 = tf::Spawn([&value, &mutex] {
            auto guard = tf::Lock(mutex);
            value = 3;
        });

        f1.Wait();
        f2.Wait();
    });

    while (!scheduler.IsDone()) {
        scheduler.Step();
    }

    // f2 should run after f1 completes due to mutex
    EXPECT_EQ(value, 3);
}

// ============================================================================
// Cleanup/Resource Management Tests
// ============================================================================

TEST(TinyFiberCleanupTest, SchedulerDestroyedWithIncompleteFibers) {
    // Test that scheduler destruction with incomplete fibers doesn't crash or leak
    int counter = 0;

    {
        auto scheduler = tf::Scheduler::Create([&counter] {
            // Spawn a fiber that will yield forever
            auto f1 = tf::Spawn([&counter] {
                counter++;
                while (true) {
                    tf::Yield();
                    counter++;
                }
            });

            // Don't wait for f1 - let it be incomplete
            tf::Yield();
            counter++;
        });

        // Run only a few steps, leaving fibers incomplete
        scheduler.Step();
        scheduler.Step();
        scheduler.Step();

        // Scheduler goes out of scope with incomplete fibers
        // This should not crash or leak memory
    }

    // Verify some work was done
    EXPECT_GT(counter, 0);
}

TEST(TinyFiberCleanupTest, SchedulerDestroyedImmediately) {
    // Test destroying scheduler without running any steps
    {
        auto scheduler = tf::Scheduler::Create([] {
            tf::Spawn([] {
                tf::Yield();
                tf::Yield();
            });
        });
        // Destroy immediately without stepping
    }
    // Should not crash
    SUCCEED();
}

TEST(TinyFiberCleanupTest, SimpleIncompleteCleanup) {
    // Very simple test - just verify cleanup of an incomplete scheduler
    {
        auto scheduler = tf::Scheduler::Create([] {
            tf::Yield();
            // This line never reached if scheduler is destroyed early
        });

        // Don't run any steps - destroy immediately
    }
    SUCCEED();
}

TEST(TinyFiberCleanupTest, FutureDestroyedBeforeCompletion) {
    // Test that Future destructor handles incomplete fiber
    {
        auto scheduler = tf::Scheduler::Create([] {
            {
                auto future = tf::Spawn([] {
                    tf::Yield();
                    tf::Yield();
                    return 42;
                });

                tf::Yield();
                // Future goes out of scope before fiber completes
                // Destructor should wait
            }
            // After this scope, future's fiber should be complete
        });

        while (!scheduler.IsDone()) {
            scheduler.Step();
        }
    }
    SUCCEED();
}

TEST(TinyFiberCleanupTest, GracefulShutdownWithStopSignal) {
    // Test that fibers receive SchedulerStoppingError and can exit gracefully
    bool fiber_caught_stop = false;
    bool fiber_cleanup_ran = false;

    {
        auto scheduler = tf::Scheduler::Create([&] {
            auto f1 = tf::Spawn([&] {
                try {
                    while (true) {
                        tf::Yield();
                    }
                } catch (const tf::SchedulerStoppingError&) {
                    fiber_caught_stop = true;
                    fiber_cleanup_ran = true;
                }
            });

            tf::Yield(); // Let f1 start
            // Main fiber exits, scheduler will be destroyed
        });

        // Run a few steps
        scheduler.Step();
        scheduler.Step();
        // Scheduler goes out of scope - should signal stop
    }

    EXPECT_TRUE(fiber_caught_stop);
    EXPECT_TRUE(fiber_cleanup_ran);
}

TEST(TinyFiberCleanupTest, ManualStop) {
    // Test manually calling Stop() before destruction
    bool fiber_exited = false;

    auto scheduler = tf::Scheduler::Create([&] {
        try {
            while (true) {
                tf::Yield();
            }
        } catch (const tf::SchedulerStoppingError&) {
            fiber_exited = true;
        }
    });

    scheduler.Step(); // Start fiber
    EXPECT_FALSE(fiber_exited);

    scheduler.Stop(); // Signal stop

    // Run to let fiber handle the stop signal
    while (!scheduler.IsDone()) {
        scheduler.Step();
    }

    EXPECT_TRUE(fiber_exited);
}

TEST(TinyFiberCleanupTest, IsStoppingCheck) {
    // Test that IsStopping() works correctly
    bool checked_stopping = false;

    auto scheduler = tf::Scheduler::Create([&] {
        while (!tf::IsStopping()) {
            tf::Yield();
        }
        checked_stopping = true;
    });

    scheduler.Step();
    EXPECT_FALSE(checked_stopping);

    scheduler.Stop();

    while (!scheduler.IsDone()) {
        scheduler.Step();
    }

    EXPECT_TRUE(checked_stopping);
}

TEST(TinyFiberCleanupTest, FibersReleasedAfterCompletion) {
    // Test that completed fibers are cleaned up and memory is released
    static int destructor_count = 0;
    destructor_count = 0;

    struct TrackDestruction {
        ~TrackDestruction() {
            destructor_count++;
        }
    };

    tf::Scheduler::Run([&] {
        // Spawn several fibers that hold resources
        for (int i = 0; i < 5; i++) {
            auto tracker = std::make_shared<TrackDestruction>();
            auto future = tf::Spawn([tracker] {
                // Fiber does some work
                tf::Yield();
            });
            future.Wait();
        }

        // After waiting, the fibers should have been cleaned up
        // Note: cleanup happens at the start of next iteration, so we yield once more
        tf::Yield();
    });

    // All trackers should be destroyed (fibers cleaned up)
    EXPECT_EQ(destructor_count, 5);
}
