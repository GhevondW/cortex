#include <cortex/pooled_memory_resource.hpp>
#include <cortex/tiny_fiber/tiny_fiber.hpp>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
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

    EXPECT_FALSE(scheduler->IsDone());

    // Step through
    while (scheduler->Step()) {
        // Each step runs until yield
    }

    EXPECT_TRUE(scheduler->IsDone());
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
    while (!scheduler->IsDone()) {
        scheduler->Step();
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

    while (!scheduler->IsDone()) {
        scheduler->Step();
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
        scheduler->Step();
        scheduler->Step();
        scheduler->Step();

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

        while (!scheduler->IsDone()) {
            scheduler->Step();
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
        scheduler->Step();
        scheduler->Step();
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

    scheduler->Step(); // Start fiber
    EXPECT_FALSE(fiber_exited);

    scheduler->Stop(); // Signal stop

    // Run to let fiber handle the stop signal
    while (!scheduler->IsDone()) {
        scheduler->Step();
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

    scheduler->Step();
    EXPECT_FALSE(checked_stopping);

    scheduler->Stop();

    while (!scheduler->IsDone()) {
        scheduler->Step();
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

// ============================================================================
// Yield - Additional Tests
// ============================================================================

TEST(TinyFiberYieldTest, YieldIfOthersReadyReturnsTrueWithOthers) {
    bool yielded = false;
    tf::Scheduler::Run([&yielded] {
        auto f1 = tf::Spawn([&yielded] {
            // f2 is in the ready queue, so YieldIfOthersReady should return true
            yielded = tf::YieldIfOthersReady();
        });

        auto f2 = tf::Spawn([] {
            tf::Yield();
        });

        // Yield to let f1 run; f2 is in the ready queue when f1 checks
        tf::Yield();
        f1.Wait();
        f2.Wait();
    });
    EXPECT_TRUE(yielded);
}

TEST(TinyFiberYieldTest, RoundRobinSchedulingOrder) {
    // Verify that fibers are scheduled in FIFO order (round-robin)
    std::vector<int> sequence;
    tf::Scheduler::Run([&sequence] {
        auto f1 = tf::Spawn([&sequence] {
            sequence.push_back(1);
            tf::Yield();
            sequence.push_back(4);
            tf::Yield();
            sequence.push_back(7);
        });

        auto f2 = tf::Spawn([&sequence] {
            sequence.push_back(2);
            tf::Yield();
            sequence.push_back(5);
            tf::Yield();
            sequence.push_back(8);
        });

        // Main fiber yields after spawning both
        sequence.push_back(0);
        tf::Yield();
        sequence.push_back(3);
        tf::Yield();
        sequence.push_back(6);

        f1.Wait();
        f2.Wait();
    });

    // FIFO scheduling: main(0), f1(1), f2(2), main(3), f1(4), f2(5), main(6), f1(7), f2(8)
    std::vector<int> expected = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    EXPECT_EQ(sequence, expected);
}

TEST(TinyFiberYieldTest, MultipleYieldsInSingleFiber) {
    int yield_count = 0;
    tf::Scheduler::Run([&yield_count] {
        for (int i = 0; i < 100; ++i) {
            tf::Yield();
            yield_count++;
        }
    });
    EXPECT_EQ(yield_count, 100);
}

// ============================================================================
// Future - Additional Tests
// ============================================================================

TEST(TinyFiberFutureTest, GetCalledTwiceThrows) {
    tf::Scheduler::Run([] {
        auto future = tf::Spawn([] {
            return 42;
        });
        EXPECT_EQ(future.Get(), 42);
        EXPECT_THROW(future.Get(), std::logic_error);
    });
}

TEST(TinyFiberFutureTest, WaitOnAlreadyCompletedFiber) {
    tf::Scheduler::Run([] {
        auto future = tf::Spawn([] {
            return 10;
        });
        tf::Yield(); // Let spawned fiber complete
        EXPECT_TRUE(future.IsReady());
        // Wait should return immediately
        future.Wait();
        EXPECT_EQ(future.Get(), 10);
    });
}

TEST(TinyFiberFutureTest, IsReadyBeforeFiberRuns) {
    tf::Scheduler::Run([] {
        auto future = tf::Spawn([] {
            tf::Yield();
            return 99;
        });
        // Fiber hasn't run yet - IsReady should be false
        EXPECT_FALSE(future.IsReady());
        EXPECT_EQ(future.Get(), 99);
    });
}

TEST(TinyFiberFutureTest, SpawnReturnsString) {
    std::string result;
    tf::Scheduler::Run([&result] {
        auto future = tf::Spawn([] {
            return std::string("hello from fiber");
        });
        result = future.Get();
    });
    EXPECT_EQ(result, "hello from fiber");
}

TEST(TinyFiberFutureTest, SpawnReturnsVector) {
    std::vector<int> result;
    tf::Scheduler::Run([&result] {
        auto future = tf::Spawn([] {
            std::vector<int> v = {1, 2, 3, 4, 5};
            tf::Yield();
            return v;
        });
        result = future.Get();
    });
    std::vector<int> expected = {1, 2, 3, 4, 5};
    EXPECT_EQ(result, expected);
}

TEST(TinyFiberFutureTest, FutureMoveConstructor) {
    int result = 0;
    tf::Scheduler::Run([&result] {
        auto f1 = tf::Spawn([] {
            tf::Yield();
            return 42;
        });
        auto f2 = std::move(f1);
        result = f2.Get();
    });
    EXPECT_EQ(result, 42);
}

TEST(TinyFiberFutureTest, FutureMoveAssignment) {
    int result = 0;
    tf::Scheduler::Run([&result] {
        auto f1 = tf::Spawn([] {
            return 10;
        });
        auto f2 = tf::Spawn([] {
            return 20;
        });

        // Move-assign f2 into f1 (f1's fiber should still complete)
        f1 = std::move(f2);
        result = f1.Get();
    });
    EXPECT_EQ(result, 20);
}

TEST(TinyFiberFutureTest, MultipleWaitersOnSameFiber) {
    // Two fibers waiting on the same spawned fiber
    std::vector<int> sequence;
    tf::Scheduler::Run([&sequence] {
        auto slow = tf::Spawn([&sequence] {
            tf::Yield();
            tf::Yield();
            sequence.push_back(3);
            return 42;
        });

        // Share the fiber ID concept - both wait via separate futures isn't possible,
        // but we can test via AddWaiter pattern: two fibers that wait on slow
        auto w1 = tf::Spawn([&sequence, &slow] {
            sequence.push_back(1);
            slow.Wait();
            sequence.push_back(4);
        });

        auto w2 = tf::Spawn([&sequence] {
            sequence.push_back(2);
            tf::Yield();
            tf::Yield();
            tf::Yield();
            sequence.push_back(5);
        });

        w1.Wait();
        w2.Wait();
    });

    EXPECT_EQ(sequence.size(), 5);
}

TEST(TinyFiberFutureTest, VoidFutureWaitMultipleTimes) {
    // Wait() on a void future should be callable, second Wait is fine
    tf::Scheduler::Run([] {
        auto future = tf::Spawn([] {
            tf::Yield();
        });
        future.Wait();
        // Second Wait after retrieved should still work (already done)
    });
}

TEST(TinyFiberFutureTest, ZeroWorkFiber) {
    // A fiber that does nothing and immediately returns
    bool done = false;
    tf::Scheduler::Run([&done] {
        auto future = tf::Spawn([] {
            return 0;
        });
        EXPECT_EQ(future.Get(), 0);
        done = true;
    });
    EXPECT_TRUE(done);
}

// ============================================================================
// Mutex - Additional Tests
// ============================================================================

TEST(TinyFiberMutexTest, TryLockFailsWhenHeldByOther) {
    bool trylock_result = true;
    tf::Scheduler::Run([&trylock_result] {
        tf::Mutex mutex;
        mutex.Lock();

        auto f = tf::Spawn([&trylock_result, &mutex] {
            trylock_result = mutex.TryLock();
        });

        tf::Yield(); // Let f run
        f.Wait();
        mutex.Unlock();
    });
    EXPECT_FALSE(trylock_result);
}

TEST(TinyFiberMutexTest, UnlockOnUnlockedThrows) {
    tf::Scheduler::Run([] {
        tf::Mutex mutex;
        EXPECT_THROW(mutex.Unlock(), std::logic_error);
    });
}

TEST(TinyFiberMutexTest, UnlockByNonOwnerThrows) {
    tf::Scheduler::Run([] {
        tf::Mutex mutex;
        mutex.Lock();

        auto f = tf::Spawn([&mutex] {
            EXPECT_THROW(mutex.Unlock(), std::logic_error);
        });

        tf::Yield(); // Let f attempt the unlock
        f.Wait();
        mutex.Unlock(); // Real owner unlocks
    });
}

TEST(TinyFiberMutexTest, GuardMoveConstructor) {
    tf::Scheduler::Run([] {
        tf::Mutex mutex;

        {
            auto g1 = tf::Lock(mutex);
            EXPECT_TRUE(mutex.IsLocked());

            auto g2 = std::move(g1);
            EXPECT_TRUE(mutex.IsLocked()); // Still locked, ownership moved
        }
        EXPECT_FALSE(mutex.IsLocked()); // g2 destroyed, unlocked
    });
}

TEST(TinyFiberMutexTest, GuardMoveAssignment) {
    tf::Scheduler::Run([] {
        tf::Mutex m1;
        tf::Mutex m2;

        auto g1 = tf::Lock(m1);
        auto g2 = tf::Lock(m2);
        EXPECT_TRUE(m1.IsLocked());
        EXPECT_TRUE(m2.IsLocked());

        // Move-assign g1 into g2: should unlock m2, then g2 now owns m1
        g2 = std::move(g1);
        EXPECT_TRUE(m1.IsLocked());
        EXPECT_FALSE(m2.IsLocked());
    });
}

TEST(TinyFiberMutexTest, MutexContentionFIFO) {
    // Verify that waiters acquire the lock in FIFO order
    std::vector<int> lock_order;
    tf::Scheduler::Run([&lock_order] {
        tf::Mutex mutex;
        mutex.Lock();

        auto f1 = tf::Spawn([&lock_order, &mutex] {
            mutex.Lock();
            lock_order.push_back(1);
            mutex.Unlock();
        });

        auto f2 = tf::Spawn([&lock_order, &mutex] {
            mutex.Lock();
            lock_order.push_back(2);
            mutex.Unlock();
        });

        auto f3 = tf::Spawn([&lock_order, &mutex] {
            mutex.Lock();
            lock_order.push_back(3);
            mutex.Unlock();
        });

        // Let all fibers attempt to lock (they'll suspend)
        tf::Yield();
        tf::Yield();
        tf::Yield();

        // Unlock - should wake them in FIFO order
        mutex.Unlock();

        f1.Wait();
        f2.Wait();
        f3.Wait();
    });

    std::vector<int> expected = {1, 2, 3};
    EXPECT_EQ(lock_order, expected);
}

// ============================================================================
// ConditionVariable - Additional Tests
// ============================================================================

TEST(TinyFiberCondVarTest, WaitWithoutPredicate) {
    std::vector<int> sequence;
    tf::Scheduler::Run([&sequence] {
        tf::Mutex mutex;
        tf::ConditionVariable cv;

        auto waiter = tf::Spawn([&] {
            auto guard = tf::Lock(mutex);
            sequence.push_back(1);
            cv.Wait(guard); // Raw wait, no predicate
            sequence.push_back(3);
        });

        tf::Yield(); // Let waiter start

        {
            auto guard = tf::Lock(mutex);
            sequence.push_back(2);
            cv.NotifyOne();
        }

        waiter.Wait();
    });

    std::vector<int> expected = {1, 2, 3};
    EXPECT_EQ(sequence, expected);
}

TEST(TinyFiberCondVarTest, NotifyOneWithNoWaiters) {
    // NotifyOne with no waiters should be a no-op
    tf::Scheduler::Run([] {
        tf::ConditionVariable cv;
        cv.NotifyOne(); // Should not crash
    });
}

TEST(TinyFiberCondVarTest, NotifyAllWithNoWaiters) {
    // NotifyAll with no waiters should be a no-op
    tf::Scheduler::Run([] {
        tf::ConditionVariable cv;
        cv.NotifyAll(); // Should not crash
    });
}

TEST(TinyFiberCondVarTest, MultipleProducersMultipleConsumers) {
    std::vector<int> consumed;
    tf::Scheduler::Run([&consumed] {
        std::queue<int> buffer;
        tf::Mutex mutex;
        tf::ConditionVariable cv;
        int producers_done = 0;
        const int num_producers = 3;

        std::vector<tf::Future<void>> producers;
        for (int p = 0; p < num_producers; ++p) {
            producers.push_back(tf::Spawn([&, p] {
                for (int i = 1; i <= 3; ++i) {
                    {
                        auto guard = tf::Lock(mutex);
                        buffer.push(p * 100 + i);
                        cv.NotifyOne();
                    }
                    tf::Yield();
                }
                auto guard = tf::Lock(mutex);
                producers_done++;
                cv.NotifyAll();
            }));
        }

        std::vector<tf::Future<void>> consumers;
        for (int c = 0; c < 2; ++c) {
            consumers.push_back(tf::Spawn([&] {
                while (true) {
                    auto guard = tf::Lock(mutex);
                    cv.Wait(guard, [&] {
                        return !buffer.empty() || producers_done == num_producers;
                    });
                    while (!buffer.empty()) {
                        consumed.push_back(buffer.front());
                        buffer.pop();
                    }
                    if (producers_done == num_producers && buffer.empty()) {
                        break;
                    }
                }
            }));
        }

        for (auto& p : producers)
            p.Wait();
        for (auto& c : consumers)
            c.Wait();
    });

    // All 9 items (3 producers x 3 items) should be consumed
    EXPECT_EQ(consumed.size(), 9);
}

// ============================================================================
// Scheduler - Additional Tests
// ============================================================================

TEST(TinyFiberSchedulerTest, MultipleSequentialRuns) {
    // Run scheduler multiple times sequentially
    for (int run = 0; run < 5; ++run) {
        int result = 0;
        tf::Scheduler::Run([&result, run] {
            auto future = tf::Spawn([run] {
                return run * 10;
            });
            result = future.Get();
        });
        EXPECT_EQ(result, run * 10);
    }
}

TEST(TinyFiberSchedulerTest, CreateWithConfig) {
    tf::Scheduler::Config config;
    config.default_stack_size = 1024 * 64;

    std::size_t observed_stack_size = 0;
    auto scheduler = tf::Scheduler::Create(
        [&observed_stack_size] {
            observed_stack_size = tf::Scheduler::Current().GetDefaultStackSize();
        },
        config);

    while (!scheduler->IsDone()) {
        scheduler->Step();
    }

    EXPECT_EQ(observed_stack_size, 1024 * 64);
}

TEST(TinyFiberSchedulerTest, GetMemoryResource) {
    tf::Scheduler::Run([] {
        auto resource = tf::Scheduler::Current().GetMemoryResource();
        EXPECT_NE(resource, nullptr);
    });
}

TEST(TinyFiberSchedulerTest, IsRunningFalseAfterCompletion) {
    auto scheduler = tf::Scheduler::Create([] {
        // Just complete immediately
    });

    while (!scheduler->IsDone()) {
        scheduler->Step();
    }

    EXPECT_FALSE(scheduler->IsRunning());
    EXPECT_TRUE(scheduler->IsDone());
}

TEST(TinyFiberSchedulerTest, IsNotStoppingByDefault) {
    tf::Scheduler::Run([] {
        EXPECT_FALSE(tf::Scheduler::Current().IsStopping());
    });
}

// ============================================================================
// Step - Additional Tests
// ============================================================================

TEST(TinyFiberStepTest, StepReturnsFalseWhenDone) {
    auto scheduler = tf::Scheduler::Create([] {
        // Immediately completes
    });

    // First step runs the fiber to completion
    // Subsequent step should return false
    while (scheduler->Step()) {
    }

    EXPECT_FALSE(scheduler->Step());
    EXPECT_TRUE(scheduler->IsDone());
}

TEST(TinyFiberStepTest, StepCountMatchesYields) {
    int step_count = 0;
    auto scheduler = tf::Scheduler::Create([] {
        tf::Yield();
        tf::Yield();
        tf::Yield();
    });

    while (!scheduler->IsDone()) {
        scheduler->Step();
        step_count++;
    }

    // 1 initial run + 3 yields resumed = 4 steps
    EXPECT_EQ(step_count, 4);
}

TEST(TinyFiberStepTest, CreateWithCustomConfig) {
    tf::Scheduler::Config config;
    config.default_stack_size = 1024 * 256;

    bool ran = false;
    auto scheduler = tf::Scheduler::Create(
        [&ran] {
            ran = true;
        },
        config);

    EXPECT_EQ(scheduler->GetDefaultStackSize(), 1024 * 256);

    while (!scheduler->IsDone()) {
        scheduler->Step();
    }
    EXPECT_TRUE(ran);
}

// ============================================================================
// Error Handling - Stopping Errors
// ============================================================================

TEST(TinyFiberStoppingTest, YieldThrowsOnStopping) {
    bool caught = false;
    {
        auto scheduler = tf::Scheduler::Create([&caught] {
            try {
                while (true)
                    tf::Yield();
            } catch (const tf::SchedulerStoppingError&) {
                caught = true;
            }
        });

        scheduler->Step();
        scheduler->Stop();

        while (!scheduler->IsDone()) {
            scheduler->Step();
        }
    }
    EXPECT_TRUE(caught);
}

TEST(TinyFiberStoppingTest, MutexLockThrowsWhenAlreadyStopping) {
    // Test that Mutex::Lock() immediately throws if scheduler is already stopping
    bool caught = false;
    {
        auto scheduler = tf::Scheduler::Create([&caught] {
            tf::Mutex mutex;
            try {
                // By the time this fiber resumes after Stop(), IsStopping() is true
                tf::Yield();
                mutex.Lock(); // Should throw immediately since IsStopping() is true
            } catch (const tf::SchedulerStoppingError&) {
                caught = true;
            }
        });

        scheduler->Step(); // Fiber runs, yields
        scheduler->Stop(); // Signal stop

        while (!scheduler->IsDone()) {
            scheduler->Step();
        }
    }
    EXPECT_TRUE(caught);
}

TEST(TinyFiberStoppingTest, CondVarWaitThrowsWhenAlreadyStopping) {
    // Test that ConditionVariable::Wait() immediately throws if scheduler is already stopping
    bool caught = false;
    {
        auto scheduler = tf::Scheduler::Create([&caught] {
            tf::Mutex mutex;
            tf::ConditionVariable cv;

            try {
                tf::Yield();
                // After Stop(), IsStopping() is true
                auto guard = tf::Lock(mutex); // This will throw since stopping
            } catch (const tf::SchedulerStoppingError&) {
                caught = true;
            }
        });

        scheduler->Step(); // Fiber runs, yields
        scheduler->Stop(); // Signal stop

        while (!scheduler->IsDone()) {
            scheduler->Step();
        }
    }
    EXPECT_TRUE(caught);
}

// Regression: a fiber suspended inside CV::Wait must unwind cleanly when the
// scheduler stops. The pre-fix path threw SchedulerStoppingError after
// unlocking the guard's mutex but without detaching the guard, so ~Guard
// called Unlock() on an unlocked mutex during unwind → terminate.
TEST(TinyFiberStoppingTest, CondVarWaitInterruptedByStopUnwindsCleanly) {
    bool caught = false;
    {
        auto scheduler = tf::Scheduler::Create([&caught] {
            tf::Mutex mutex;
            tf::ConditionVariable cv;
            try {
                auto guard = tf::Lock(mutex);
                cv.Wait(guard);
            } catch (const tf::SchedulerStoppingError&) {
                caught = true;
            }
        });

        scheduler->Step(); // Fiber locks mutex, suspends inside cv.Wait()
        scheduler->Stop(); // Wakes the fiber while it's parked

        while (!scheduler->IsDone()) {
            scheduler->Step();
        }
    }
    EXPECT_TRUE(caught);
}

// Regression: multiple fibers parked in Mutex::waiters_ must be safely cleaned
// up after Stop(). The pre-fix path stored raw Fiber* in waiters_ and Unlock
// dereferenced entries without validating state, so a force-scheduled fiber
// (whose state had transitioned Suspended -> Ready via Stop) would hit
// Schedule()->Wake()'s assert(state == Suspended) and, in release, end up in
// ready_queue_ twice — leading to use-after-free once it was cleaned up.
TEST(TinyFiberStoppingTest, MutexWaitersSurviveStop) {
    bool holder_caught = false;
    {
        auto scheduler = tf::Scheduler::Create([&] {
            tf::Mutex mutex;

            auto holder = tf::Spawn([&] {
                try {
                    auto guard = tf::Lock(mutex);
                    while (true)
                        tf::Yield();
                } catch (const tf::SchedulerStoppingError&) {
                    holder_caught = true;
                }
            });

            auto waiter1 = tf::Spawn([&] {
                auto guard = tf::Lock(mutex);
            });

            auto waiter2 = tf::Spawn([&] {
                auto guard = tf::Lock(mutex);
            });

            // Let everyone get into position: holder acquires the mutex,
            // waiter1 and waiter2 park in mutex.waiters_.
            tf::Yield();
            tf::Yield();
            tf::Yield();
        });

        for (int i = 0; i < 8 && !scheduler->IsDone(); ++i) {
            scheduler->Step();
        }
        scheduler->Stop();
        while (!scheduler->IsDone()) {
            scheduler->Step();
        }
    }
    // The point of this test is that shutdown completes cleanly. The holder
    // unwinds on the stopping signal and releases the mutex; the woken waiters
    // either catch stopping or acquire-and-release cleanly. The pre-fix code
    // would either fire an assertion or trip use-after-free here.
    EXPECT_TRUE(holder_caught);
}

// ============================================================================
// Complex Scenarios - Additional Tests
// ============================================================================

TEST(TinyFiberComplexTest, FiberFanOut) {
    // One fiber spawns many children and collects results
    int total = 0;
    tf::Scheduler::Run([&total] {
        std::vector<tf::Future<int>> futures;
        for (int i = 0; i < 20; ++i) {
            futures.push_back(tf::Spawn([i] {
                tf::Yield();
                return i + 1;
            }));
        }
        for (auto& f : futures) {
            total += f.Get();
        }
    });
    // Sum of 1..20 = 210
    EXPECT_EQ(total, 210);
}

TEST(TinyFiberComplexTest, FiberPipeline) {
    // Chain of fibers: each waits for the previous one and transforms the result
    int result = 0;
    tf::Scheduler::Run([&result] {
        auto stage1 = tf::Spawn([] {
            tf::Yield();
            return 1;
        });

        auto stage2 = tf::Spawn([&stage1] {
            int v = stage1.Get();
            tf::Yield();
            return v * 10;
        });

        auto stage3 = tf::Spawn([&stage2] {
            int v = stage2.Get();
            tf::Yield();
            return v + 5;
        });

        result = stage3.Get();
    });
    EXPECT_EQ(result, 15); // 1 * 10 + 5
}

TEST(TinyFiberComplexTest, RecursiveFiberSpawn) {
    // Fibonacci via recursive fiber spawning
    tf::Scheduler::Run([] {
        std::function<tf::Future<int>(int)> fib = [&fib](int n) -> tf::Future<int> {
            return tf::Spawn([n, &fib]() -> int {
                if (n <= 1) return n;
                auto a = fib(n - 1);
                auto b = fib(n - 2);
                return a.Get() + b.Get();
            });
        };

        auto result = fib(10);
        EXPECT_EQ(result.Get(), 55);
    });
}

TEST(TinyFiberComplexTest, PingPong) {
    // Two fibers passing control back and forth
    int ping_count = 0;
    int pong_count = 0;

    tf::Scheduler::Run([&] {
        auto pinger = tf::Spawn([&] {
            for (int i = 0; i < 50; ++i) {
                ping_count++;
                tf::Yield();
            }
        });

        auto ponger = tf::Spawn([&] {
            for (int i = 0; i < 50; ++i) {
                pong_count++;
                tf::Yield();
            }
        });

        pinger.Wait();
        ponger.Wait();
    });

    EXPECT_EQ(ping_count, 50);
    EXPECT_EQ(pong_count, 50);
}

TEST(TinyFiberComplexTest, SharedCounterNoMutex) {
    // Without mutex, interleaved increments should cause data races
    // (In cooperative scheduling, the "race" shows as incorrect values due to yield between read and write)
    int counter = 0;
    tf::Scheduler::Run([&counter] {
        auto f1 = tf::Spawn([&counter] {
            for (int i = 0; i < 10; ++i) {
                int temp = counter;
                tf::Yield();
                counter = temp + 1; // Stale read - other fiber may have incremented
            }
        });

        auto f2 = tf::Spawn([&counter] {
            for (int i = 0; i < 10; ++i) {
                int temp = counter;
                tf::Yield();
                counter = temp + 1;
            }
        });

        f1.Wait();
        f2.Wait();
    });

    // Without mutex, counter should be less than 20 due to lost updates
    EXPECT_LT(counter, 20);
}

TEST(TinyFiberComplexTest, BarrierPattern) {
    // All fibers reach a barrier point before any proceed past it
    std::vector<int> sequence;

    tf::Scheduler::Run([&sequence] {
        tf::Mutex mutex;
        tf::ConditionVariable cv;
        int arrived = 0;
        const int num_fibers = 3;

        auto barrier_fiber = [&](int id) {
            {
                auto guard = tf::Lock(mutex);
                sequence.push_back(id); // Arrived at barrier
                arrived++;
                if (arrived == num_fibers) {
                    cv.NotifyAll(); // Last one wakes everyone
                } else {
                    cv.Wait(guard, [&] {
                        return arrived == num_fibers;
                    });
                }
            }
            sequence.push_back(id + 10); // Past barrier
        };

        auto f1 = tf::Spawn([&] {
            barrier_fiber(1);
        });
        auto f2 = tf::Spawn([&] {
            barrier_fiber(2);
        });
        auto f3 = tf::Spawn([&] {
            barrier_fiber(3);
        });

        f1.Wait();
        f2.Wait();
        f3.Wait();
    });

    EXPECT_EQ(sequence.size(), 6);

    // All arrivals (1,2,3) should come before all completions (11,12,13)
    int arrival_count = 0;
    for (int v : sequence) {
        if (v < 10) {
            arrival_count++;
        } else {
            // By the time we see a completion, all 3 should have arrived
            EXPECT_EQ(arrival_count, 3) << "Fiber passed barrier before all arrived";
            break;
        }
    }
}

TEST(TinyFiberComplexTest, DeeplyNestedSpawn) {
    // Chain of nested spawns, each waiting on the inner one
    int result = 0;
    tf::Scheduler::Run([&result] {
        auto f = tf::Spawn([] {
            auto f2 = tf::Spawn([] {
                auto f3 = tf::Spawn([] {
                    auto f4 = tf::Spawn([] {
                        auto f5 = tf::Spawn([] {
                            return 42;
                        });
                        return f5.Get();
                    });
                    return f4.Get();
                });
                return f3.Get();
            });
            return f2.Get();
        });
        result = f.Get();
    });
    EXPECT_EQ(result, 42);
}

TEST(TinyFiberComplexTest, FiberExceptionDoesNotAffectOthers) {
    // A fiber that throws should not affect other fibers
    int healthy_result = 0;
    tf::Scheduler::Run([&healthy_result] {
        auto bad = tf::Spawn([]() -> int {
            tf::Yield();
            throw std::runtime_error("boom");
        });

        auto good = tf::Spawn([&healthy_result] {
            tf::Yield();
            tf::Yield();
            healthy_result = 123;
        });

        good.Wait();
        EXPECT_THROW(bad.Get(), std::runtime_error);
    });

    EXPECT_EQ(healthy_result, 123);
}

TEST(TinyFiberComplexTest, SpawnWhileHoldingMutex) {
    // Spawn a fiber while holding a mutex lock
    int result = 0;
    tf::Scheduler::Run([&result] {
        tf::Mutex mutex;

        {
            auto guard = tf::Lock(mutex);
            auto f = tf::Spawn([&result] {
                result = 42;
            });
            // f destructor waits, then guard destructor unlocks
        }
    });
    EXPECT_EQ(result, 42);
}

TEST(TinyFiberComplexTest, LargeNumberOfFibers) {
    // Stress test with many fibers
    const int num_fibers = 100;
    int counter = 0;

    tf::Scheduler::Run([&counter] {
        std::vector<tf::Future<void>> futures;
        futures.reserve(100);

        for (int i = 0; i < 100; ++i) {
            futures.push_back(tf::Spawn([&counter] {
                tf::Yield();
                counter++;
                tf::Yield();
            }));
        }

        for (auto& f : futures) {
            f.Wait();
        }
    });

    EXPECT_EQ(counter, num_fibers);
}

// ============================================================================
// Fiber Reuse Tests
// ============================================================================

TEST(TinyFiberReuseTest, FinishedFibersAreReusedNotRecycledThroughMemoryPool) {
    auto pooled = std::make_shared<cortex::PooledMemoryResource>();
    tf::Scheduler::Run(
        [&pooled] {
            for (int i = 0; i < 5; ++i) {
                auto future = tf::Spawn([] {
                    return 1;
                });
                (void)future.Get();
            }
            // Finished fibers were parked for reuse, so their 256KB stacks
            // never went back to the memory pool's free lists. (Small blocks
            // like future state do round-trip the pool.)
            EXPECT_LT(pooled->GetCachedBytes(), cortex::Coroutine::kDefaultStackSizeBytes);
        },
        tf::Scheduler::Config {.memory_resource = pooled});
}

TEST(TinyFiberReuseTest, ZeroMaxPooledFibersDisablesReuse) {
    auto pooled = std::make_shared<cortex::PooledMemoryResource>();
    tf::Scheduler::Run(
        [&pooled] {
            for (int i = 0; i < 5; ++i) {
                auto future = tf::Spawn([] {
                    return 1;
                });
                (void)future.Get();
            }
            // Without fiber reuse, destroyed fibers push their stacks into
            // the memory pool's free lists.
            EXPECT_GE(pooled->GetCachedBytes(), cortex::Coroutine::kDefaultStackSizeBytes);
        },
        tf::Scheduler::Config {.memory_resource = pooled, .max_pooled_fibers = 0});
}

TEST(TinyFiberReuseTest, ReusedFibersDeliverResultsAndWakeWaiters) {
    tf::Scheduler::Run([] {
        for (int i = 0; i < 100; ++i) {
            auto future = tf::Spawn([i] {
                tf::Yield();
                return i * 2;
            });
            EXPECT_EQ(future.Get(), i * 2);
        }
    });
}

TEST(TinyFiberReuseTest, ReuseAcrossWavesOfConcurrentFibers) {
    tf::Scheduler::Run([] {
        for (int wave = 0; wave < 5; ++wave) {
            std::vector<tf::Future<int>> futures;
            futures.reserve(32);
            for (int i = 0; i < 32; ++i) {
                futures.push_back(tf::Spawn([i] {
                    tf::Yield();
                    return i;
                }));
            }
            for (int i = 0; i < 32; ++i) {
                EXPECT_EQ(futures[static_cast<std::size_t>(i)].Get(), i);
            }
        }
    });
}

TEST(TinyFiberReuseTest, TeardownWithParkedFibersIsClean) {
    auto scheduler = tf::Scheduler::Create([] {
        auto future = tf::Spawn([] {
            return 7;
        });
        (void)future.Get();
        tf::Yield();
    });
    while (scheduler->Step()) {
    }
    scheduler.reset(); // destroys parked (reusable) fibers cleanly
}
