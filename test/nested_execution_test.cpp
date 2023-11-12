#define BOOST_TEST_MODULE cortex_nested_execution_test
#include <boost/test/included/unit_test.hpp>
#include <cortex/basic_flow.hpp>
#include <cortex/execution.hpp>
#include <cortex/stack_allocator.hpp>

#include <iostream>

BOOST_AUTO_TEST_SUITE(cortex_nested_execution_test_suite)

BOOST_AUTO_TEST_CASE(nested) {
    using namespace cortex;

    int counter = 0;
    static constexpr std::size_t stack_size = 1000000;

    auto execution_one =
        execution::create(stack_allocator(stack_size), basic_flow::make([&counter](api::disabler& dis) {
                              BOOST_CHECK_EQUAL(++counter, 2);
                              std::cout << "Step 2" << '\n';
                              dis.disable();

                              BOOST_CHECK_EQUAL(++counter, 4);
                              std::cout << "Step 4" << '\n';
                          }));

    auto execution_two =
        execution::create(stack_allocator(stack_size), basic_flow::make([&execution_one, &counter](api::disabler& dis) {
                              BOOST_CHECK_EQUAL(++counter, 1);
                              std::cout << "Step 1" << '\n';
                              execution_one.enable();

                              BOOST_CHECK_EQUAL(++counter, 3);
                              std::cout << "Step 3" << '\n';
                              execution_one.enable();

                              auto nested = execution::create(stack_allocator(stack_size),
                                                              basic_flow::make([&counter](api::disabler& dis) {
                                                                  BOOST_CHECK_EQUAL(++counter, 5);
                                                                  std::cout << "Step 5" << '\n';
                                                                  dis.disable();

                                                                  BOOST_CHECK_EQUAL(++counter, 7);
                                                                  std::cout << "Step 7" << '\n';
                                                              }));

                              nested.enable();

                              BOOST_CHECK_EQUAL(++counter, 6);
                              std::cout << "Step 6" << '\n';
                              nested.enable();
                          }));

    execution_two.enable();

    BOOST_CHECK_EQUAL(counter, 7);
}

BOOST_AUTO_TEST_SUITE_END()
