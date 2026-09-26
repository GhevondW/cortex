#pragma once

#include <cortex/tiny_fiber/detail/future_state.hpp>
#include <cortex/tiny_fiber/future.hpp>

#include <array>
#include <cstddef>
#include <ranges>
#include <type_traits>
#include <vector>

/**
 * @file wait.hpp
 * @brief Waiting on several futures at once.
 */

namespace cortex::tiny_fiber {

namespace detail {
// Waits for one future's state; an empty (detached, moved-from) future is ready.
inline void AwaitIfPresent(FutureStateBase* state) {
    if (state != nullptr) {
        AwaitState(*state, /*cancellable=*/true);
    }
}

template <typename T>
struct IsFuture : std::false_type {};

template <typename T>
struct IsFuture<Future<T>> : std::true_type {};

// A tiny_fiber::Future<T>, for any T.
template <typename T>
concept AnyFuture = IsFuture<std::remove_cv_t<T>>::value;

// A range of futures: std::vector, std::array, std::span, ...
template <typename R>
concept FutureRange = std::ranges::forward_range<R> && AnyFuture<std::ranges::range_value_t<R>>;
} // namespace detail

/**
 * @brief Block the current fiber until every future is ready.
 *
 * Does not rethrow the fibers' exceptions; call Get() on each for results.
 * Empty futures (detached or moved-from) count as ready.
 *
 * @code
 * auto a = tf::Spawn(LoadConfig);
 * auto b = tf::Spawn(LoadAssets);
 * tf::WaitAll(a, b);
 * @endcode
 *
 * @throws CancelledError / SchedulerStoppingError like Future::Wait().
 */
template <detail::AnyFuture... Futures>
void WaitAll(Futures&... futures) {
    (detail::AwaitIfPresent(futures.StateInternal()), ...);
}

/**
 * @brief Block the current fiber until every future in a container
 *        (std::vector, std::array, std::span, ...) is ready.
 *
 * An empty container returns at once.
 *
 * @throws CancelledError / SchedulerStoppingError like Future::Wait().
 */
template <detail::FutureRange Range>
void WaitAll(Range&& futures) {
    for (auto& future : futures) {
        detail::AwaitIfPresent(future.StateInternal());
    }
}

/**
 * @brief Block the current fiber until at least one future is ready.
 *
 * Does not cancel the others; call Cancel() on the ones you no longer need.
 *
 * @return The index of a ready future (the lowest, if several are).
 * @throws CancelledError / SchedulerStoppingError like Future::Wait().
 */
template <detail::AnyFuture... Futures>
std::size_t WaitAny(Futures&... futures) {
    static_assert(sizeof...(Futures) > 0, "WaitAny() needs at least one future");
    const std::array<detail::FutureStateBase*, sizeof...(Futures)> states {futures.StateInternal()...};
    return detail::AwaitAnyState(states.data(), states.size());
}

/**
 * @brief Block the current fiber until at least one future in a container
 *        (std::vector, std::array, std::span, ...) is ready.
 *
 * @return The index of a ready future (the lowest, if several are).
 * @throws std::invalid_argument if the container is empty.
 * @throws CancelledError / SchedulerStoppingError like Future::Wait().
 */
template <detail::FutureRange Range>
std::size_t WaitAny(Range&& futures) {
    std::vector<detail::FutureStateBase*> states;
    for (auto& future : futures) {
        states.push_back(future.StateInternal());
    }
    return detail::AwaitAnyState(states.data(), states.size());
}

} // namespace cortex::tiny_fiber
