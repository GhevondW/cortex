#pragma once

#include <cortex/tiny_fiber/detail/future_state.hpp>
#include <cortex/tiny_fiber/future.hpp>

#include <array>
#include <cstddef>
#include <span>
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
} // namespace detail

/**
 * @brief Block the current fiber until every future is ready.
 *
 * Does not rethrow the fibers' exceptions; call Get() on each for results.
 * Empty futures (detached or moved-from) count as ready.
 */
template <typename... Futures>
void WaitAll(Futures&... futures) {
    (detail::AwaitIfPresent(futures.StateInternal()), ...);
}

/**
 * @brief Block the current fiber until every future in `futures` is ready.
 */
template <typename T>
void WaitAll(std::span<Future<T>> futures) {
    for (auto& future : futures) {
        detail::AwaitIfPresent(future.StateInternal());
    }
}

/**
 * @brief Block the current fiber until at least one future is ready.
 *
 * @return The index of a ready future (the lowest, if several are).
 * @throws CancelledError / SchedulerStoppingError like Future::Wait().
 */
template <typename... Futures>
std::size_t WaitAny(Futures&... futures) {
    static_assert(sizeof...(Futures) > 0, "WaitAny() needs at least one future");
    const std::array<detail::FutureStateBase*, sizeof...(Futures)> states {futures.StateInternal()...};
    return detail::AwaitAnyState(states.data(), states.size());
}

/**
 * @brief Block the current fiber until at least one future in `futures` is
 *        ready; returns its index.
 *
 * @throws std::invalid_argument if `futures` is empty.
 */
template <typename T>
std::size_t WaitAny(std::span<Future<T>> futures) {
    std::vector<detail::FutureStateBase*> states;
    states.reserve(futures.size());
    for (auto& future : futures) {
        states.push_back(future.StateInternal());
    }
    return detail::AwaitAnyState(states.data(), states.size());
}

} // namespace cortex::tiny_fiber
