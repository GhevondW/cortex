#include <cortex/basic_flow.hpp>
#include <cortex/execution.hpp>
#include <cortex/stack_allocator.hpp>
#include <gtest/gtest.h>

#include <iostream>

TEST(CortexNestedExecutionTest, Nested) {
    using namespace cortex;

    int counter = 0;
    static constexpr std::size_t stack_size = 1000000;

    auto execution_one = execution::create(stack_allocator::create(stack_size),
                                           basic_flow::make([&counter](api::suspendable& suspender) {
                                               EXPECT_EQ(++counter, 2);
                                               std::cout << "Step 2" << '\n';
                                               suspender.suspend();

                                               EXPECT_EQ(++counter, 4);
                                               std::cout << "Step 4" << '\n';
                                           }));

    auto execution_two = execution::create(
        stack_allocator::create(stack_size), basic_flow::make([&execution_one, &counter](api::suspendable& suspender) {
            EXPECT_EQ(++counter, 1);
            std::cout << "Step 1" << '\n';
            execution_one.resume();

            EXPECT_EQ(++counter, 3);
            std::cout << "Step 3" << '\n';
            execution_one.resume();

            auto nested = execution::create(stack_allocator::create(stack_size),
                                            basic_flow::make([&counter](api::suspendable& suspender) {
                                                EXPECT_EQ(++counter, 5);
                                                std::cout << "Step 5" << '\n';
                                                suspender.suspend();

                                                EXPECT_EQ(++counter, 7);
                                                std::cout << "Step 7" << '\n';
                                            }));

            nested.resume();

            EXPECT_EQ(++counter, 6);
            std::cout << "Step 6" << '\n';
            nested.resume();
        }));

    execution_two.resume();

    EXPECT_EQ(counter, 7);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
