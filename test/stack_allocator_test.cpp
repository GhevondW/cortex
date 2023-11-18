#define BOOST_TEST_MODULE cortex_stack_allocator_test
#include <boost/test/included/unit_test.hpp>
#include <cortex/error.hpp>
#include <cortex/stack_allocator.hpp>

using namespace cortex;

BOOST_AUTO_TEST_SUITE(cortex_stack_allocator_test_suite)

BOOST_AUTO_TEST_CASE(constructor_valid_size) {
    BOOST_CHECK_NO_THROW(stack_allocator::create(512));
}

BOOST_AUTO_TEST_CASE(constructor_zero_size) {
    BOOST_CHECK_THROW(stack_allocator::create(0), cortex::error);
}

BOOST_AUTO_TEST_CASE(allocate_valid_size) {
    auto allocator = stack_allocator::create(512);
    stack st;
    BOOST_CHECK(st.empty());
    BOOST_CHECK(st.size() == 0);
    BOOST_CHECK(st.top() == nullptr);
    try {
        st = allocator.allocate();
    } catch (...) {
        BOOST_CHECK(false); // We must not reach here.
    }

    allocator.deallocate(st);
}

BOOST_AUTO_TEST_CASE(deallocate_valid_stack) {
    auto allocator = stack_allocator::create(512);
    stack stack = allocator.allocate();
    BOOST_CHECK_NO_THROW(allocator.deallocate(stack));
}

BOOST_AUTO_TEST_SUITE_END()
