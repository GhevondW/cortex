#include <cortex/basic_flow.hpp>
#include <cortex/execution.hpp>
#include <cortex/stack_allocator.hpp>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>

namespace {

struct echo {
    explicit echo(int* c)
        : counter(c)
        , arr() {
        *counter = 111;
    }

    ~echo() {
        *counter = 222;
    }

    int* counter = nullptr;
    int arr[64 * 64];
};

} // namespace

TEST(CortexMemoryLeakTest, NoMemoryLeak) {
    using namespace cortex;

    int counter = 0;

    {
        auto flow = basic_flow::make([&counter](api::suspendable& suspender) mutable {
            std::cout << "Step 2" << '\n';

            std::unique_ptr<echo> e(new echo(&counter));
            EXPECT_EQ(counter, 111);

            suspender.suspend();

            EXPECT_FALSE(true); // We must not reach here
        });

        auto execution = execution::create(stack_allocator::create(1000000), std::move(flow));

        std::cout << "Step 1" << '\n';
        execution.resume();
        EXPECT_EQ(counter, 111);

        std::cout << "Step 3" << '\n';
    }

    EXPECT_EQ(counter, 222);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
