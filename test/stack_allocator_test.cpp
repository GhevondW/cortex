#include <cortex/error.hpp>
#include <cortex/stack_allocator.hpp>
#include <gtest/gtest.h>

using namespace cortex;

TEST(CortexStackAllocatorTest, ConstructorValidSize) {
    ASSERT_NO_THROW(stack_allocator::create(512));
}

TEST(CortexStackAllocatorTest, ConstructorZeroSize) {
    ASSERT_THROW(stack_allocator::create(0), cortex::error);
}

TEST(CortexStackAllocatorTest, AllocateValidSize) {
    auto allocator = stack_allocator::create(512);
    stack st;
    EXPECT_TRUE(st.empty());
    EXPECT_EQ(st.size(), 0);
    EXPECT_EQ(st.top(), nullptr);

    try {
        st = allocator.allocate();
    } catch (...) {
        EXPECT_FALSE(true); // We must not reach here.
    }

    allocator.deallocate(st);
}

TEST(CortexStackAllocatorTest, DeallocateValidStack) {
    auto allocator = stack_allocator::create(512);
    stack st = allocator.allocate();
    ASSERT_NO_THROW(allocator.deallocate(st));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
