// Timers and time-budgeted driving: SleepFor/SleepUntil, RunFor/RunUntil,
// NextTimerDeadline and the kWaiting status.

#include <cortex/tiny_fiber/tiny_fiber.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <stdexcept>
#include <vector>

namespace tf = cortex::tiny_fiber;
using namespace std::chrono_literals;

namespace {

// Deterministic time for tests that must not depend on the machine's speed.
tf::Scheduler::TimePoint g_now {};

tf::Scheduler::TimePoint FakeNow() {
    return g_now;
}

tf::Scheduler::Config FakeClockConfig() {
    g_now = tf::Scheduler::TimePoint {} + 1h; // arbitrary non-zero origin
    tf::Scheduler::Config config;
    config.clock = &FakeNow;
    return config;
}

} // namespace

TEST(TinyFiberTimer, SleepingFiberWaitsForItsDeadline) {
    bool woke = false;
    auto scheduler = tf::Scheduler::Create(
        [&] {
            tf::SleepFor(10ms);
            woke = true;
        },
        FakeClockConfig());
    const auto start = g_now;

    EXPECT_EQ(scheduler->RunFor(1ms), tf::Scheduler::Status::kWaiting);
    ASSERT_TRUE(scheduler->NextTimerDeadline().has_value());
    EXPECT_EQ(*scheduler->NextTimerDeadline(), start + 10ms);

    g_now = start + 9ms;
    EXPECT_EQ(scheduler->RunFor(1ms), tf::Scheduler::Status::kWaiting);
    EXPECT_FALSE(woke);

    g_now = start + 10ms;
    EXPECT_EQ(scheduler->GetStatus(), tf::Scheduler::Status::kRunnable); // the timer is due
    EXPECT_EQ(scheduler->RunFor(1ms), tf::Scheduler::Status::kDone);
    EXPECT_TRUE(woke);
    EXPECT_FALSE(scheduler->NextTimerDeadline().has_value());
}

TEST(TinyFiberTimer, TimersFireInDeadlineOrder) {
    std::vector<int> order;
    auto scheduler = tf::Scheduler::Create(
        [&] {
            for (int ms : {30, 10, 20}) {
                tf::SpawnDetached([&order, ms] {
                    tf::SleepFor(std::chrono::milliseconds(ms));
                    order.push_back(ms);
                });
            }
        },
        FakeClockConfig());
    EXPECT_EQ(scheduler->RunFor(1ms), tf::Scheduler::Status::kWaiting);
    g_now += 100ms;
    EXPECT_EQ(scheduler->RunFor(1ms), tf::Scheduler::Status::kDone);
    EXPECT_EQ(order, (std::vector<int> {10, 20, 30}));
}

TEST(TinyFiberTimer, SleepUntilUsesSchedulerClock) {
    bool woke = false;
    auto scheduler = tf::Scheduler::Create(
        [&] {
            tf::SleepUntil(g_now + 5ms);
            woke = true;
        },
        FakeClockConfig());
    scheduler->RunFor(1ms);
    EXPECT_FALSE(woke);
    g_now += 5ms;
    scheduler->RunFor(1ms);
    EXPECT_TRUE(woke);
}

TEST(TinyFiberTimer, RunBlocksUntilTimersFire) {
    const auto start = std::chrono::steady_clock::now();
    tf::Scheduler::Run([] {
        tf::SleepFor(20ms);
    });
    EXPECT_GE(std::chrono::steady_clock::now() - start, 20ms);
}

TEST(TinyFiberTimer, SleepForZeroYields) {
    std::vector<int> order;
    tf::Scheduler::Run([&] {
        auto other = tf::Spawn([&] {
            order.push_back(2);
            tf::SleepFor(0ms);
            order.push_back(4);
        });
        order.push_back(1);
        tf::SleepFor(0ms);
        order.push_back(3);
        other.Wait();
    });
    EXPECT_EQ(order, (std::vector<int> {1, 2, 3, 4}));
}

TEST(TinyFiberTimer, StopWakesSleepersAndClearsTimers) {
    bool stopped = false;
    auto scheduler = tf::Scheduler::Create([&] {
        try {
            tf::SleepFor(1h);
        } catch (const tf::SchedulerStoppingError&) {
            stopped = true;
        }
    });
    scheduler->Step();
    ASSERT_TRUE(scheduler->NextTimerDeadline().has_value());
    scheduler->Stop();
    while (!scheduler->IsDone()) {
        scheduler->Step();
    }
    EXPECT_TRUE(stopped);
    EXPECT_FALSE(scheduler->NextTimerDeadline().has_value());
}

TEST(TinyFiberTimer, RunForReturnsWhenBudgetSpent) {
    auto scheduler = tf::Scheduler::Create([] {
        for (;;) {
            tf::Yield();
        }
    });
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(scheduler->RunFor(2ms), tf::Scheduler::Status::kRunnable);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_GE(elapsed, 2ms);
    EXPECT_LT(elapsed, 1s);
}

TEST(TinyFiberTimer, SleepOutsideFiberThrows) {
    EXPECT_THROW(tf::SleepFor(1ms), std::logic_error);
}

TEST(TinyFiberTimer, DeadlockIsNotConfusedWithWaiting) {
    auto scheduler = tf::Scheduler::Create([] {
        tf::SpawnDetached([] {
            tf::SleepFor(1h);
        });
        tf::Mutex mutex;
        tf::ConditionVariable cv;
        auto guard = tf::Lock(mutex);
        cv.Wait(guard);
    });
    scheduler->RunFor(1ms);
    EXPECT_EQ(scheduler->GetStatus(), tf::Scheduler::Status::kWaiting); // the sleeper will wake
}

// --- CheckPoint: budget-aware yielding ----------------------------------------

TEST(TinyFiberCheckPoint, NoOpOutsideFibers) {
    EXPECT_NO_THROW(tf::CheckPoint());
}

TEST(TinyFiberCheckPoint, YieldsOnlyWhenSliceIsSpent) {
    auto config = FakeClockConfig();
    config.time_slice = 2ms;
    int iterations = 0;
    auto scheduler = tf::Scheduler::Create(
        [&] {
            for (int i = 0; i < 1000; ++i) {
                ++iterations;
                if (i == 500) {
                    g_now += 3ms; // this iteration used up the slice
                }
                tf::CheckPoint();
            }
        },
        config);

    scheduler->Step(); // runs until a checkpoint notices the spent slice
    EXPECT_GE(iterations, 501);
    EXPECT_LT(iterations, 1000);

    while (scheduler->Step()) {
    }
    EXPECT_EQ(iterations, 1000);
}

TEST(TinyFiberCheckPoint, RespectsRunForDeadline) {
    tf::Scheduler::Config config;
    config.time_slice = 1h; // only the RunFor budget can end the slice
    auto scheduler = tf::Scheduler::Create(
        [] {
            for (;;) {
                tf::CheckPoint();
            }
        },
        config);
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(scheduler->RunFor(1ms), tf::Scheduler::Status::kRunnable);
    EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
    scheduler->Stop();
    while (!scheduler->IsDone()) {
        scheduler->Step();
    }
}

TEST(TinyFiberCheckPoint, ThrowsWhenStopping) {
    bool stopped = false;
    auto scheduler = tf::Scheduler::Create([&] {
        try {
            for (;;) {
                tf::CheckPoint();
                tf::Yield();
            }
        } catch (const tf::SchedulerStoppingError&) {
            stopped = true;
        }
    });
    scheduler->Step();
    scheduler->Stop();
    while (!scheduler->IsDone()) {
        scheduler->Step();
    }
    EXPECT_TRUE(stopped);
}

TEST(TinyFiberCheckPoint, SameFunctionRunsSyncAndCooperative) {
    auto work = [] {
        long sum = 0;
        for (int i = 0; i < 100000; ++i) {
            sum += i;
            tf::CheckPoint();
        }
        return sum;
    };
    const long sync = work(); // plain call: CheckPoint is a no-op
    EXPECT_EQ(tf::Scheduler::Run(work), sync);
}

TEST(TinyFiberCheckPoint, YieldsTheInnermostFiber) {
    int inner_steps = 0;
    bool outer_finished = false;
    tf::Scheduler::Run([&] {
        tf::Scheduler::Config config;
        config.time_slice = 0ms; // every checkpoint yields
        auto inner = tf::Scheduler::Create(
            [] {
                for (int i = 0; i < 3; ++i) {
                    tf::CheckPoint();
                }
            },
            config);
        while (inner->Step()) {
            ++inner_steps;
        }
        outer_finished = true;
    });
    EXPECT_GE(inner_steps, 3);
    EXPECT_TRUE(outer_finished);
}

// A cheap loop must not teach CheckPoint() to skip clock reads for so long
// that a later expensive loop overruns its slice by orders of magnitude.
TEST(TinyFiberCheckPoint, ExpensiveFiberAfterCheapFiberStillYieldsOnTime) {
    auto config = FakeClockConfig();
    config.time_slice = 2ms;
    int expensive_iterations_in_first_step = -1;
    int expensive_iterations = 0;
    auto scheduler = tf::Scheduler::Create(
        [&] {
            auto cheap = tf::Spawn([] {
                for (int i = 0; i < 100000; ++i) {
                    tf::CheckPoint(); // free: the fake clock never moves here
                }
            });
            cheap.Wait();
            tf::SpawnDetached([&] {
                for (int i = 0; i < 50; ++i) {
                    g_now += 1ms; // each iteration costs 1 ms
                    ++expensive_iterations;
                    tf::CheckPoint();
                }
            });
        },
        config);
    // Step until the expensive fiber has run its first slice.
    while (expensive_iterations == 0 && scheduler->Step()) {
    }
    expensive_iterations_in_first_step = expensive_iterations;
    EXPECT_LE(expensive_iterations_in_first_step, 4); // ~2 ms slice, not 4096 iterations
    while (scheduler->Step()) {
    }
    EXPECT_EQ(expensive_iterations, 50);
}

TEST(TinyFiberCheckPoint, ExpensiveLoopAfterCheapLoopInSameFiberYieldsOnTime) {
    auto config = FakeClockConfig();
    config.time_slice = 2ms;
    int expensive_iterations = 0;
    auto scheduler = tf::Scheduler::Create(
        [&] {
            for (int i = 0; i < 100000; ++i) {
                tf::CheckPoint(); // a cheap phase...
            }
            for (int i = 0; i < 50; ++i) {
                g_now += 1ms; // ...then an expensive one, same slice
                ++expensive_iterations;
                tf::CheckPoint();
            }
        },
        config);
    scheduler->Step();
    EXPECT_LE(expensive_iterations, 4);
    while (scheduler->Step()) {
    }
    EXPECT_EQ(expensive_iterations, 50);
}
