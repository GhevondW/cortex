#define BOOST_TEST_MODULE cortex_memory_leak_test
#include <boost/test/included/unit_test.hpp>
#include <cortex/basic_flow.hpp>
#include <cortex/execution.hpp>
#include <cortex/stack_allocator.hpp>
#include <iostream>
#include <memory>

namespace {

struct echo {
    echo(int* c)
        : counter(c) {
        *counter = 111;
    }

    ~echo() {
        *counter = 222;
    }

    int* counter = nullptr;
    int arr[64 * 64];
};

} // namespace

BOOST_AUTO_TEST_SUITE(cortex_memory_leak_test_suite)

BOOST_AUTO_TEST_CASE(no_memory_leak) {
    using namespace cortex;

    int counter = 0;

    {
        auto flow = basic_flow::make([&counter](api::disabler& dis) mutable {
            std::cout << "Step 2" << '\n';

            std::unique_ptr<echo> e(new echo(&counter));
            BOOST_CHECK_EQUAL(counter, 111);

            dis.disable();

            BOOST_CHECK(false); // We must not reach here
        });

        auto execution = execution::create(stack_allocator {1000000}, std::move(flow));

        std::cout << "Step 1" << '\n';
        execution.enable();
        BOOST_CHECK_EQUAL(counter, 111);

        std::cout << "Step 3" << '\n';
    }

    BOOST_CHECK_EQUAL(counter, 222);
}

BOOST_AUTO_TEST_SUITE_END()
