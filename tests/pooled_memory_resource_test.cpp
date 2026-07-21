#include <cortex/pooled_memory_resource.hpp>
#include <cortex/tiny_fiber/tiny_fiber.hpp>
#include <gtest/gtest.h>

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

TEST(PooledMemoryResourceTest, ReusesBlocksOfSameSize) {
    auto tracker = std::make_shared<TrackingResource>();
    cortex::PooledMemoryResource pool({.max_cached_bytes = 1024 * 1024, .upstream = tracker});

    void* first = pool.Allocate(256);
    pool.Deallocate(first, 256);
    void* second = pool.Allocate(256);

    EXPECT_EQ(first, second);
    EXPECT_EQ(tracker->allocations, 1);
    EXPECT_EQ(tracker->deallocations, 0);

    pool.Deallocate(second, 256);
}

TEST(PooledMemoryResourceTest, TracksCachedBytes) {
    cortex::PooledMemoryResource pool;

    void* p = pool.Allocate(512);
    EXPECT_EQ(pool.GetCachedBytes(), 0u);

    pool.Deallocate(p, 512);
    EXPECT_EQ(pool.GetCachedBytes(), 512u);

    void* reused = pool.Allocate(512);
    EXPECT_EQ(pool.GetCachedBytes(), 0u);
    pool.Deallocate(reused, 512);
}

TEST(PooledMemoryResourceTest, RespectsMaxCachedBytes) {
    auto tracker = std::make_shared<TrackingResource>();
    cortex::PooledMemoryResource pool({.max_cached_bytes = 256, .upstream = tracker});

    void* p1 = pool.Allocate(256);
    void* p2 = pool.Allocate(256);

    pool.Deallocate(p1, 256); // cached (256 <= cap)
    pool.Deallocate(p2, 256); // over cap: released upstream

    EXPECT_EQ(pool.GetCachedBytes(), 256u);
    EXPECT_EQ(tracker->deallocations, 1);
}

TEST(PooledMemoryResourceTest, PassesThroughExtendedAlignment) {
    auto tracker = std::make_shared<TrackingResource>();
    cortex::PooledMemoryResource pool({.max_cached_bytes = 1024 * 1024, .upstream = tracker});

    constexpr std::size_t kBigAlignment = 2 * alignof(std::max_align_t);

    void* p = pool.Allocate(256, kBigAlignment);
    pool.Deallocate(p, 256, kBigAlignment);

    EXPECT_EQ(pool.GetCachedBytes(), 0u);
    EXPECT_EQ(tracker->allocations, 1);
    EXPECT_EQ(tracker->deallocations, 1);
}

TEST(PooledMemoryResourceTest, ReleasesCacheOnDestruction) {
    auto tracker = std::make_shared<TrackingResource>();
    {
        cortex::PooledMemoryResource pool({.max_cached_bytes = 1024 * 1024, .upstream = tracker});
        void* p1 = pool.Allocate(128);
        void* p2 = pool.Allocate(4096);
        pool.Deallocate(p1, 128);
        pool.Deallocate(p2, 4096);
        EXPECT_EQ(tracker->deallocations, 0);
    }
    EXPECT_EQ(tracker->allocations, tracker->deallocations);
}

// End-to-end: sequentially spawned fibers must recycle stacks through the
// scheduler's pooled resource instead of allocating fresh ones each time.
TEST(PooledMemoryResourceTest, SchedulerReusesFiberStacks) {
    namespace tf = cortex::tiny_fiber;

    auto tracker = std::make_shared<TrackingResource>();
    auto pool = cortex::MakePooledMemoryResource({
        .max_cached_bytes = 64u * 1024u * 1024u,
        .upstream = tracker,
    });

    tf::Scheduler::Run(
        [] {
            for (int i = 0; i < 50; ++i) {
                auto future = tf::Spawn([] {
                    tf::Yield();
                    return 1;
                });
                EXPECT_EQ(future.Get(), 1);
            }
        },
        tf::Scheduler::Config {.memory_resource = pool});

    // 50 sequential spawns re-use the first fiber's blocks: the upstream sees
    // roughly one allocation per distinct block size, not one per spawn.
    EXPECT_LT(tracker->allocations, 16u);
}
