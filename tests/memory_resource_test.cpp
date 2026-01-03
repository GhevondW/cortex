#include <cortex/memory_resource.hpp>
#include <gtest/gtest.h>

namespace {

class TrackingResource : public cortex::MemoryResource {
public:
    size_t allocations = 0;
    size_t deallocations = 0;
    size_t bytes_allocated = 0;

protected:
    void* DoAllocate(std::size_t bytes, std::size_t alignment) override {
        allocations++;
        bytes_allocated += bytes;
        // Use default to actually get memory
        return cortex::GetDefaultMemoryResource()->Allocate(bytes, alignment);
    }

    void DoDeallocate(void* p, std::size_t bytes, std::size_t alignment) override {
        deallocations++;
        cortex::GetDefaultMemoryResource()->Deallocate(p, bytes, alignment);
    }
};

} // namespace

TEST(MemoryResourceTest, DefaultResource) {
    auto resource = cortex::GetDefaultMemoryResource();
    ASSERT_NE(resource, nullptr);

    void* p = resource->Allocate(100, 16);
    ASSERT_NE(p, nullptr);
    // Check alignment
    ASSERT_EQ(reinterpret_cast<uintptr_t>(p) % 16, 0);

    resource->Deallocate(p, 100, 16);
}

TEST(MemoryResourceTest, TrackingResource) {
    auto tracker = std::make_shared<TrackingResource>();

    void* p1 = tracker->Allocate(64);
    void* p2 = tracker->Allocate(128);

    EXPECT_EQ(tracker->allocations, 2);
    EXPECT_EQ(tracker->bytes_allocated, 64 + 128);

    tracker->Deallocate(p1, 64);
    tracker->Deallocate(p2, 128);

    EXPECT_EQ(tracker->deallocations, 2);
}
