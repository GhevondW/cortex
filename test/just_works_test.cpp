#include <cortex/basic_flow.hpp>
#include <cortex/execution.hpp>
#include <cortex/stack_allocator.hpp>
#include <gtest/gtest.h>
#include <iostream>

// Define a test suite for the cortex library
TEST(CortexJustWorksTest, JustWorks) {
    using namespace cortex;

    int counter = 0;

    auto flow = basic_flow::make([&counter](api::suspendable& suspender) {
        EXPECT_EQ(++counter, 2);
        std::cout << "Step 2" << '\n';
        suspender.suspend();

        EXPECT_EQ(++counter, 4);
        std::cout << "Step 4" << '\n';
        suspender.suspend();

        EXPECT_EQ(++counter, 6);
        std::cout << "Step 6" << '\n';
        suspender.suspend();

        EXPECT_EQ(++counter, 8);
        std::cout << "Step 8" << '\n';
        suspender.suspend();
    });

    auto execution = execution::create(stack_allocator::create(1000000), std::move(flow));

    EXPECT_EQ(++counter, 1);
    std::cout << "Step 1" << '\n';
    execution.resume();

    EXPECT_EQ(++counter, 3);
    std::cout << "Step 3" << '\n';
    execution.resume();

    EXPECT_EQ(++counter, 5);
    std::cout << "Step 5" << '\n';
    execution.resume();

    EXPECT_EQ(++counter, 7);
    std::cout << "Step 7" << '\n';
    execution.resume();
}

TEST(CortexJustWorksTest, JustWorksPartial) {
    using namespace cortex;

    int counter = 0;

    auto flow = basic_flow::make([&counter](api::suspendable& suspender) {
        EXPECT_EQ(++counter, 2);
        std::cout << "Step 2" << '\n';
        suspender.suspend();

        EXPECT_FALSE(true); // We must not reach here
    });

    auto execution = execution::create(stack_allocator::create(1000000), std::move(flow));

    EXPECT_EQ(++counter, 1);
    std::cout << "Step 1" << '\n';
    execution.resume();

    EXPECT_EQ(++counter, 3);
    std::cout << "Step 3" << '\n';
}

TEST(CortexJustWorksTest, CreateExceptions) {
    using namespace cortex;
    EXPECT_THROW(execution::create_with_raw_flow(stack_allocator::create(1000000), nullptr), execution::invalid_flow);
    EXPECT_THROW(execution::create(stack_allocator::create(100), basic_flow::make([](api::suspendable& s) {})),
                 execution::invalid_stack_size);
}

TEST(CortexJustWorksTest, CreateWithRawFlow) {
    using namespace cortex;

    int counter = 0;

    class flow_class : public api::flow {
    public:
        explicit flow_class(int& counter)
            : _counter(counter) {}

        void run(api::suspendable& suspender) override {
            EXPECT_EQ(++_counter, 2);
            std::cout << "Step 2" << '\n';
            suspender.suspend();

            EXPECT_EQ(++_counter, 4);
            std::cout << "Step 4" << '\n';
            suspender.suspend();

            EXPECT_EQ(++_counter, 6);
            std::cout << "Step 6" << '\n';
            suspender.suspend();

            EXPECT_EQ(++_counter, 8);
            std::cout << "Step 8" << '\n';
            suspender.suspend();
        }

    private:
        int& _counter;
    };

    flow_class flow(counter);
    execution exec = execution::create_with_raw_flow(stack_allocator::create(1000000), &flow);

    EXPECT_EQ(++counter, 1);
    std::cout << "Step 1" << '\n';
    exec.resume();

    EXPECT_EQ(++counter, 3);
    std::cout << "Step 3" << '\n';
    exec.resume();

    EXPECT_EQ(++counter, 5);
    std::cout << "Step 5" << '\n';
    exec.resume();

    EXPECT_EQ(++counter, 7);
    std::cout << "Step 7" << '\n';
    exec.resume();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
