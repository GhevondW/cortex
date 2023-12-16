#include <cortex/basic_flow.hpp>
#include <cortex/error.hpp>
#include <cortex/execution.hpp>
#include <cortex/stack_allocator.hpp>
#include <gtest/gtest.h>

using namespace cortex;

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

TEST(CortexRethrowExceptionTest, JustRethrowsUnhandledException) {
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

        throw aux::my_error("My Error");
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

    EXPECT_THROW(execution.resume(), aux::my_error);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
