// Waiter containers stay bounded and cheap: waits that end early (timeouts,
// WaitAny, cancellation) leave stale tokens behind, and pruning them must be
// amortized so many live waiters don't make every registration expensive.

#include <cortex/tiny_fiber/detail/wait_queue.hpp>

#include <gtest/gtest.h>

#include <cstdint>

namespace detail = cortex::tiny_fiber::detail;

namespace {

detail::WaiterRef Token(std::uint64_t n) {
    return detail::WaiterRef {n + 1, n};
}

} // namespace

TEST(TinyFiberWaitQueue, StaleTokensDoNotAccumulate) {
    detail::WaitQueue queue;
    const auto stale = [](detail::WaiterRef) {
        return false;
    };
    for (std::uint64_t i = 0; i < 10000; ++i) {
        queue.Push(Token(i), stale);
    }
    EXPECT_LT(queue.Size(), 64u);
}

TEST(TinyFiberWaitQueue, LiveTokensAreKeptInOrderWithLinearPruningWork) {
    detail::WaitQueue queue;
    std::uint64_t checks = 0;
    const auto live = [&checks](detail::WaiterRef) {
        ++checks;
        return true;
    };
    for (std::uint64_t i = 0; i < 10000; ++i) {
        queue.Push(Token(i), live);
    }
    EXPECT_EQ(queue.Size(), 10000u);
    EXPECT_LT(checks, 3u * 10000u);
}

TEST(TinyFiberWaiterList, StaleTokensDoNotAccumulate) {
    detail::WaiterList list;
    const auto stale = [](detail::WaiterRef) {
        return false;
    };
    for (std::uint64_t i = 0; i < 10000; ++i) {
        list.Push(Token(i), stale);
    }
    EXPECT_LT(list.Size(), 64u);
}

TEST(TinyFiberWaiterList, ManyLiveWaitersCostLinearPruningWork) {
    detail::WaiterList list;
    std::uint64_t checks = 0;
    const auto live = [&checks](detail::WaiterRef) {
        ++checks;
        return true;
    };
    for (std::uint64_t i = 0; i < 10000; ++i) {
        list.Push(Token(i), live);
    }
    EXPECT_EQ(list.Size(), 10000u);
    EXPECT_LT(checks, 3u * 10000u); // re-pruning on every push would be ~5e7
}
