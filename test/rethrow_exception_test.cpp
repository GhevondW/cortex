#define BOOST_TEST_MODULE cortex_rethrow_exception_test
#include <boost/test/included/unit_test.hpp>
#include <cortex/basic_flow.hpp>
#include <cortex/error.hpp>
#include <cortex/execution.hpp>
#include <cortex/stack_allocator.hpp>

using namespace cortex;

BOOST_AUTO_TEST_SUITE(cortex_cortex_rethrow_exception_test_suite)

namespace aux {

struct my_error : std::exception {
    template <typename StringLike>
    explicit my_error(StringLike&& str)
        : _what(std::forward<StringLike>(str)) {}

    [[nodiscard]] const char* what() const noexcept override {
        return _what.c_str();
    }

private:
    std::string _what;
};

} // namespace aux

BOOST_AUTO_TEST_CASE(just_rethrows_unhandled_exception) {
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

        throw aux::my_error("My Error");
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

    BOOST_CHECK_THROW(execution.enable(), aux::my_error);
}

BOOST_AUTO_TEST_SUITE_END()
