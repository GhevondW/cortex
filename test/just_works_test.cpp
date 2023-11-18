#define BOOST_TEST_MODULE cortex_just_works_test
#include <boost/test/included/unit_test.hpp>
#include <cortex/basic_flow.hpp>
#include <cortex/execution.hpp>
#include <cortex/stack_allocator.hpp>
#include <iostream>

// Define a test suite for the cortex library
BOOST_AUTO_TEST_SUITE(cortex_just_works_test_suite)

// Test case for basic execution flow
BOOST_AUTO_TEST_CASE(just_works) {
    using namespace cortex;

    int counter = 0;

    auto flow = basic_flow::make([&counter](api::disabler& dis) {
        BOOST_CHECK_EQUAL(++counter, 2);
        std::cout << "Step 2" << '\n';
        dis.disable();

        BOOST_CHECK_EQUAL(++counter, 4);
        std::cout << "Step 4" << '\n';
        dis.disable();

        BOOST_CHECK_EQUAL(++counter, 6);
        std::cout << "Step 6" << '\n';
        dis.disable();

        BOOST_CHECK_EQUAL(++counter, 8);
        std::cout << "Step 8" << '\n';
        dis.disable();
    });

    auto execution = execution::create(stack_allocator::create(1000000), std::move(flow));

    BOOST_CHECK_EQUAL(++counter, 1);
    std::cout << "Step 1" << '\n';
    execution.enable();

    BOOST_CHECK_EQUAL(++counter, 3);
    std::cout << "Step 3" << '\n';
    execution.enable();

    BOOST_CHECK_EQUAL(++counter, 5);
    std::cout << "Step 5" << '\n';
    execution.enable();

    BOOST_CHECK_EQUAL(++counter, 7);
    std::cout << "Step 7" << '\n';
    execution.enable();
}

BOOST_AUTO_TEST_CASE(just_works_partial) {
    using namespace cortex;

    int counter = 0;

    auto flow = basic_flow::make([&counter](api::disabler& dis) {
        BOOST_CHECK_EQUAL(++counter, 2);
        std::cout << "Step 2" << '\n';
        dis.disable();

        BOOST_CHECK(false); // We must not reach here
    });

    auto execution = execution::create(stack_allocator::create(1000000), std::move(flow));

    BOOST_CHECK_EQUAL(++counter, 1);
    std::cout << "Step 1" << '\n';
    execution.enable();

    BOOST_CHECK_EQUAL(++counter, 3);
    std::cout << "Step 3" << '\n';
}

BOOST_AUTO_TEST_CASE(create_exceptions) {
    using namespace cortex;
    BOOST_CHECK_THROW(execution::create(stack_allocator::create(1000000), nullptr), execution::invalid_flow);
    BOOST_CHECK_THROW(execution::create(stack_allocator::create(100), basic_flow::make([](api::disabler& dis) {})),
                      execution::invalid_stack_size);
}

BOOST_AUTO_TEST_SUITE_END()
