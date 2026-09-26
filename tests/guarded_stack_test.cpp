// Fiber stacks get a guard page: overflowing a stack must crash on the spot
// instead of silently corrupting whatever memory lies below it.

#include <cortex/guarded_stack_resource.hpp>
#include <cortex/tiny_fiber/tiny_fiber.hpp>

#include <gtest/gtest.h>

namespace tf = cortex::tiny_fiber;

namespace {

// Deep, non-tail recursion with a large frame: overflows a 64 KB stack fast.
int Recurse(int depth) {
    volatile char frame[1024];
    frame[0] = static_cast<char>(depth);
    if (depth == 0) {
        return frame[0];
    }
    return Recurse(depth - 1) + frame[0];
}

} // namespace

TEST(GuardedStack, SupportedOnThisPlatform) {
    EXPECT_TRUE(cortex::HasGuardPageSupport());
}

TEST(GuardedStack, StackSizedBlocksAreFullyUsable) {
    auto resource = cortex::MakeGuardedStackResource();
    constexpr std::size_t kSize = 256 * 1024 + 123; // not a page multiple
    auto* block = static_cast<unsigned char*>(resource->Allocate(kSize));
    block[0] = 1;
    block[kSize - 1] = 2;
    EXPECT_EQ(block[0] + block[kSize - 1], 3);
    resource->Deallocate(block, kSize);
}

TEST(GuardedStack, SmallBlocksGoToUpstream) {
    auto resource = cortex::MakeGuardedStackResource();
    void* small = resource->Allocate(64, 8);
    ASSERT_NE(small, nullptr);
    resource->Deallocate(small, 64, 8);
}

TEST(GuardedStackDeathTest, WritingBelowAGuardedBlockFaults) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_DEATH(
        {
            auto resource = cortex::MakeGuardedStackResource();
            auto* block = static_cast<volatile unsigned char*>(resource->Allocate(64 * 1024));
            block[-1] = 1;
        },
        "");
}

// A small overflow is the dangerous one: it lands in neighbouring memory and
// nothing notices. One byte below a stack from the scheduler's default
// resource must fault.
TEST(GuardedStackDeathTest, WritingJustBelowADefaultFiberStackFaults) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_DEATH(
        {
            auto resource = tf::MakeDefaultFiberResource();
            auto* stack = static_cast<volatile unsigned char*>(resource->Allocate(64 * 1024));
            stack[-1] = 1;
        },
        "");
}

// End-to-end: runaway recursion in a fiber dies instead of scribbling.
TEST(GuardedStackDeathTest, FiberStackOverflowCrashesInsteadOfCorrupting) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_DEATH(
        {
            tf::Scheduler::Config config;
            config.default_stack_size = 64 * 1024;
            tf::Scheduler::Run([] { return Recurse(100000); }, config);
        },
        "");
}
