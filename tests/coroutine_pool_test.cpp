#include <cortex/config.hpp>
#include <cortex/coroutine_pool.hpp>
#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#ifndef CORTEX_EMSCRIPTEN
#include <atomic>
#include <thread>
#endif

namespace {

class TrackingResource : public cortex::MemoryResource {
public:
    size_t allocations = 0;
    size_t deallocations = 0;

protected:
    void* DoAllocate(std::size_t bytes, std::size_t alignment) override {
        allocations++;
        return cortex::GetDefaultMemoryResource()->Allocate(bytes, alignment);
    }

    void DoDeallocate(void* p, std::size_t bytes, std::size_t alignment) override {
        deallocations++;
        cortex::GetDefaultMemoryResource()->Deallocate(p, bytes, alignment);
    }
};

} // namespace

TEST(CoroutinePoolTest, AcquireRunsBody) {
    cortex::LocalCoroutinePool pool;
    int runs = 0;
    auto coroutine = pool.Acquire([&runs](cortex::CoroutineSuspendContext&) {
        ++runs;
    });
    EXPECT_FALSE(coroutine.IsDone());
    coroutine.Resume();
    EXPECT_TRUE(coroutine.IsDone());
    EXPECT_EQ(runs, 1);
}

TEST(CoroutinePoolTest, SuspendAndResume) {
    cortex::LocalCoroutinePool pool;
    std::vector<int> sequence;
    auto coroutine = pool.Acquire([&sequence](cortex::CoroutineSuspendContext& ctx) {
        sequence.push_back(1);
        ctx.Suspend();
        sequence.push_back(2);
    });
    coroutine.Resume();
    EXPECT_EQ(sequence, (std::vector<int> {1}));
    coroutine.Resume();
    EXPECT_EQ(sequence, (std::vector<int> {1, 2}));
    EXPECT_TRUE(coroutine.IsDone());
}

TEST(CoroutinePoolTest, ReleaseParksAndAcquireReuses) {
    auto tracker = std::make_shared<TrackingResource>();
    cortex::LocalCoroutinePool pool({.memory_resource = tracker});

    {
        auto coroutine = pool.Acquire([](cortex::CoroutineSuspendContext&) {});
        coroutine.Resume();
    } // handle destruction releases to the pool
    EXPECT_EQ(pool.GetParkedCount(), 1u);

    const auto allocations_after_first = tracker->allocations;
    {
        auto coroutine = pool.Acquire([](cortex::CoroutineSuspendContext&) {});
        coroutine.Resume();
    }
    EXPECT_EQ(tracker->allocations, allocations_after_first);
    EXPECT_EQ(tracker->deallocations, 0u);
    EXPECT_EQ(pool.GetParkedCount(), 1u);
}

TEST(CoroutinePoolTest, RebindRunsNewBodyOnSameCoroutine) {
    cortex::LocalCoroutinePool pool;
    int first = 0;
    int second = 0;
    auto coroutine = pool.Acquire([&first](cortex::CoroutineSuspendContext&) {
        first = 1;
    });
    coroutine.Resume();
    EXPECT_TRUE(coroutine.IsDone());

    coroutine.Rebind([&second](cortex::CoroutineSuspendContext&) {
        second = 2;
    });
    EXPECT_FALSE(coroutine.IsDone());
    coroutine.Resume();
    EXPECT_TRUE(coroutine.IsDone());
    EXPECT_EQ(first, 1);
    EXPECT_EQ(second, 2);
}

TEST(CoroutinePoolTest, RebindBeforeFirstResumeReplacesBody) {
    cortex::LocalCoroutinePool pool;
    bool original_ran = false;
    bool replacement_ran = false;
    auto coroutine = pool.Acquire([&original_ran](cortex::CoroutineSuspendContext&) {
        original_ran = true;
    });
    coroutine.Rebind([&replacement_ran](cortex::CoroutineSuspendContext&) {
        replacement_ran = true;
    });
    coroutine.Resume();
    EXPECT_FALSE(original_ran);
    EXPECT_TRUE(replacement_ran);
}

TEST(CoroutinePoolTest, RebindThrowsWhileBodyIsSuspended) {
    cortex::LocalCoroutinePool pool;
    auto coroutine = pool.Acquire([](cortex::CoroutineSuspendContext& ctx) {
        ctx.Suspend();
    });
    coroutine.Resume(); // suspends inside the body
    EXPECT_FALSE(coroutine.IsDone());
    EXPECT_THROW(coroutine.Rebind([](cortex::CoroutineSuspendContext&) {}), std::logic_error);
    coroutine.Resume(); // let it finish
    EXPECT_TRUE(coroutine.IsDone());
}

TEST(CoroutinePoolTest, ExceptionPropagatesAndCoroutineStaysReusable) {
    cortex::LocalCoroutinePool pool;
    auto coroutine = pool.Acquire([](cortex::CoroutineSuspendContext&) {
        throw std::runtime_error("boom");
    });
    EXPECT_THROW(coroutine.Resume(), std::runtime_error);
    EXPECT_TRUE(coroutine.IsDone());

    bool ran = false;
    coroutine.Rebind([&ran](cortex::CoroutineSuspendContext&) {
        ran = true;
    });
    coroutine.Resume();
    EXPECT_TRUE(ran);
}

TEST(CoroutinePoolTest, ReleaseUnfinishedBodyUnwindsStack) {
    cortex::LocalCoroutinePool pool;
    bool destroyed = false;
    struct Sentinel {
        bool* flag;
        ~Sentinel() {
            *flag = true;
        }
    };
    {
        auto coroutine = pool.Acquire([&destroyed](cortex::CoroutineSuspendContext& ctx) {
            Sentinel sentinel {&destroyed};
            ctx.Suspend();
            ctx.Suspend();
        });
        coroutine.Resume();
        EXPECT_FALSE(destroyed);
    } // released mid-body: unwinds, then parks
    EXPECT_TRUE(destroyed);
    EXPECT_EQ(pool.GetParkedCount(), 1u);

    bool ran = false;
    auto coroutine = pool.Acquire([&ran](cortex::CoroutineSuspendContext&) {
        ran = true;
    });
    coroutine.Resume();
    EXPECT_TRUE(ran);
}

TEST(CoroutinePoolTest, MaxParkedEvictsExcessCoroutines) {
    auto tracker = std::make_shared<TrackingResource>();
    cortex::LocalCoroutinePool pool({.max_parked = 1, .memory_resource = tracker});

    auto a = pool.Acquire([](cortex::CoroutineSuspendContext&) {});
    auto b = pool.Acquire([](cortex::CoroutineSuspendContext&) {});
    a.Resume();
    b.Resume();
    a.Release();
    b.Release(); // over the cap: destroyed

    EXPECT_EQ(pool.GetParkedCount(), 1u);
    EXPECT_GT(tracker->deallocations, 0u);
}

TEST(CoroutinePoolTest, ReservePrewarmsWithoutRunning) {
    auto tracker = std::make_shared<TrackingResource>();
    cortex::LocalCoroutinePool pool({.memory_resource = tracker});
    pool.Reserve(4);
    EXPECT_EQ(pool.GetParkedCount(), 4u);

    const auto allocations_after_reserve = tracker->allocations;
    for (int i = 0; i < 4; ++i) {
        auto coroutine = pool.Acquire([](cortex::CoroutineSuspendContext&) {});
        coroutine.Resume();
    }
    EXPECT_EQ(tracker->allocations, allocations_after_reserve);
}

TEST(CoroutinePoolTest, ReserveIsCappedByMaxParked) {
    cortex::LocalCoroutinePool pool({.max_parked = 2});
    pool.Reserve(8);
    EXPECT_EQ(pool.GetParkedCount(), 2u);
}

TEST(CoroutinePoolTest, HandleOutlivesPool) {
    auto tracker = std::make_shared<TrackingResource>();
    std::optional<cortex::LocalPooledCoroutine> handle;
    {
        cortex::LocalCoroutinePool pool({.memory_resource = tracker});
        handle.emplace(pool.Acquire([](cortex::CoroutineSuspendContext& ctx) {
            ctx.Suspend();
        }));
        handle->Resume();
    } // pool destroyed; handle still owns its coroutine
    handle->Resume();
    EXPECT_TRUE(handle->IsDone());
    handle.reset(); // destroys the coroutine instead of parking
    EXPECT_EQ(tracker->allocations, tracker->deallocations);
}

TEST(CoroutinePoolTest, PoolDestructorReleasesParkedCoroutines) {
    auto tracker = std::make_shared<TrackingResource>();
    {
        cortex::LocalCoroutinePool pool({.memory_resource = tracker});
        pool.Reserve(3);
        auto coroutine = pool.Acquire([](cortex::CoroutineSuspendContext&) {});
        coroutine.Resume();
    }
    EXPECT_EQ(tracker->allocations, tracker->deallocations);
}

TEST(CoroutinePoolTest, GetStackSizeReportsConfiguredSize) {
    cortex::LocalCoroutinePool pool({.stack_size_bytes = 128 * 1024});
    auto coroutine = pool.Acquire([](cortex::CoroutineSuspendContext&) {});
    EXPECT_EQ(coroutine.GetStackSize(), 128u * 1024u);
}

TEST(CoroutinePoolTest, DestroyingNeverResumedCoroutineDoesNotRunBody) {
    bool ran = false;
    {
        cortex::LocalCoroutinePool pool;
        auto coroutine = pool.Acquire([&ran](cortex::CoroutineSuspendContext& ctx) {
            ran = true;
            ctx.Suspend();
        });
    } // released without Resume, then pool destroyed
    EXPECT_FALSE(ran);
}

TEST(CoroutinePoolTest, WarmReleaseWithoutResumeDoesNotRunBody) {
    cortex::LocalCoroutinePool pool;
    {
        auto warm = pool.Acquire([](cortex::CoroutineSuspendContext&) {});
        warm.Resume();
    } // parks a started coroutine

    bool ran = false;
    {
        auto coroutine = pool.Acquire([&ran](cortex::CoroutineSuspendContext&) {
            ran = true;
        });
    } // released without Resume: must NOT run the body
    EXPECT_FALSE(ran);

    // The parked coroutine is still usable afterwards.
    bool reused_ran = false;
    auto coroutine = pool.Acquire([&reused_ran](cortex::CoroutineSuspendContext&) {
        reused_ran = true;
    });
    coroutine.Resume();
    EXPECT_TRUE(reused_ran);
}

TEST(CoroutinePoolTest, DoubleRebindBeforeResumeIsAllowedOnWarmCoroutine) {
    cortex::LocalCoroutinePool pool;
    {
        auto warm = pool.Acquire([](cortex::CoroutineSuspendContext&) {});
        warm.Resume();
    } // the next Acquire pops a warm (already started) coroutine

    bool first_ran = false;
    bool second_ran = false;
    auto coroutine = pool.Acquire([&first_ran](cortex::CoroutineSuspendContext&) {
        first_ran = true;
    });
    coroutine.Rebind([&second_ran](cortex::CoroutineSuspendContext&) {
        second_ran = true;
    });
    coroutine.Resume();
    EXPECT_FALSE(first_ran);
    EXPECT_TRUE(second_ran);
}

#ifndef CORTEX_EMSCRIPTEN
TEST(CoroutinePoolTest, ThreadSafePoolParallelAcquireRelease) {
    cortex::CoroutinePool pool({.max_parked = 8});
    std::atomic<int> completed {0};

    constexpr int kThreads = 4;
    constexpr int kIterationsPerThread = 250;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&pool, &completed] {
            for (int i = 0; i < kIterationsPerThread; ++i) {
                auto coroutine = pool.Acquire([&completed](cortex::CoroutineSuspendContext& ctx) {
                    ctx.Suspend();
                    completed.fetch_add(1, std::memory_order_relaxed);
                });
                coroutine.Resume();
                coroutine.Resume();
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(completed.load(), kThreads * kIterationsPerThread);
}
#endif
