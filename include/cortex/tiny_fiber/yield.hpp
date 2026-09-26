#pragma once

#include <chrono>
#include <string_view>

/**
 * @file yield.hpp
 * @brief Yield functions for cooperative multitasking.
 */

namespace cortex::tiny_fiber {

/**
 * @brief Yield control to other ready fibers.
 *
 * The current fiber is placed at the back of the ready queue.
 * Must be called from within a fiber.
 *
 * @throws std::logic_error if called outside of a fiber.
 * @throws SchedulerStoppingError if the scheduler is stopping.
 */
void Yield();

/**
 * @brief Yield only if there are other ready fibers.
 *
 * @return true if yielded, false if no other fibers are ready.
 * @throws std::logic_error if called outside of a fiber.
 * @throws SchedulerStoppingError if the scheduler is stopping.
 */
bool YieldIfOthersReady();

/**
 * @brief Check if the current scheduler is stopping.
 *
 * Fibers can use this to exit gracefully during shutdown.
 *
 * @return true if the scheduler is stopping, false otherwise.
 * @throws std::logic_error if called outside of a fiber.
 */
bool IsStopping();

namespace detail {
void SleepForImpl(std::chrono::steady_clock::duration duration);
void SleepUntilImpl(std::chrono::steady_clock::time_point deadline);
} // namespace detail

/**
 * @brief Suspend the current fiber for at least `duration`; other fibers run
 *        meanwhile. A zero or negative duration just yields.
 *
 * Time comes from the scheduler's Config::clock.
 *
 * @throws std::logic_error if called outside of a fiber.
 * @throws SchedulerStoppingError if the scheduler is stopping.
 */
template <typename Rep, typename Period>
void SleepFor(std::chrono::duration<Rep, Period> duration) {
    detail::SleepForImpl(std::chrono::ceil<std::chrono::steady_clock::duration>(duration));
}

/**
 * @brief Suspend the current fiber until `deadline` (a time point of the
 *        scheduler's clock, see Scheduler::Now()).
 *
 * @throws std::logic_error if called outside of a fiber.
 * @throws SchedulerStoppingError if the scheduler is stopping.
 */
template <typename Duration>
void SleepUntil(std::chrono::time_point<std::chrono::steady_clock, Duration> deadline) {
    detail::SleepUntilImpl(std::chrono::ceil<std::chrono::steady_clock::duration>(deadline));
}

/**
 * @brief Name the current fiber. The name appears in
 *        Scheduler::DescribeFibers() and in DeadlockError messages.
 *
 * @throws std::logic_error if called outside of a fiber.
 */
void SetFiberName(std::string_view name);

} // namespace cortex::tiny_fiber
