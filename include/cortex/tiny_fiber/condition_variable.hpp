#pragma once

#include <cortex/tiny_fiber/detail/duration.hpp>
#include <cortex/tiny_fiber/detail/fiber.hpp>
#include <cortex/tiny_fiber/detail/wait_queue.hpp>
#include <cortex/tiny_fiber/mutex.hpp>
#include <cortex/tiny_fiber/scheduler.hpp>

#include <chrono>

/**
 * @file condition_variable.hpp
 * @brief Cooperative condition variable for tiny_fiber.
 */

namespace cortex::tiny_fiber {

/**
 * @class ConditionVariable
 * @brief A cooperative condition variable.
 *
 * Allows fibers to wait for a condition to be signaled by another fiber.
 */
class ConditionVariable {
public:
    ConditionVariable() = default;
    ~ConditionVariable();

    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;

    /**
     * @brief Wait until notified.
     *
     * The mutex must be locked by the current fiber. It will be
     * unlocked while waiting and re-locked before returning.
     *
     * Unlike std::condition_variable, a wait that ends because the fiber was
     * cancelled or the scheduler stops throws *without* re-locking: the
     * mutex is released and `guard` no longer owns it. The same holds for
     * WaitUntil() and WaitFor().
     *
     * @param guard The lock guard holding the mutex.
     * @throws CancelledError if the fiber was cancelled (Future::Cancel()).
     * @throws SchedulerStoppingError if the scheduler is stopping.
     */
    void Wait(Mutex::Guard& guard);

    /**
     * @brief Wait until notified and predicate is true.
     *
     * @param guard The lock guard holding the mutex.
     * @param pred The predicate to check.
     */
    template <typename Predicate>
    void Wait(Mutex::Guard& guard, Predicate pred) {
        while (!pred()) {
            Wait(guard);
        }
    }

    /**
     * @brief Wait until notified or until the scheduler's clock reaches
     *        `deadline`. The mutex is re-locked before returning normally.
     *
     * @return true if notified, false if the deadline passed.
     * @throws CancelledError / SchedulerStoppingError like Wait(), without
     *         the mutex.
     */
    bool WaitUntil(Mutex::Guard& guard, Scheduler::TimePoint deadline);

    /**
     * @brief Wait until notified or `timeout` elapsed.
     *
     * @return true if notified, false on timeout.
     */
    template <typename Rep, typename Period>
    bool WaitFor(Mutex::Guard& guard, std::chrono::duration<Rep, Period> timeout) {
        return WaitUntil(guard, DeadlineAfter(detail::SaturatingCeil(timeout)));
    }

    /**
     * @brief Wait until `pred()` holds or `timeout` elapsed.
     *
     * @return The final value of `pred()`.
     */
    template <typename Rep, typename Period, typename Predicate>
    bool WaitFor(Mutex::Guard& guard, std::chrono::duration<Rep, Period> timeout, Predicate pred) {
        const auto deadline = DeadlineAfter(detail::SaturatingCeil(timeout));
        while (!pred()) {
            if (!WaitUntil(guard, deadline)) {
                return pred();
            }
        }
        return true;
    }

    /**
     * @brief Wake one waiting fiber. Call it from a fiber of the scheduler
     *        (it throws std::logic_error from plain code).
     */
    void NotifyOne();

    /**
     * @brief Wake all waiting fibers. Call it from a fiber of the scheduler
     *        (it throws std::logic_error from plain code).
     */
    void NotifyAll();

private:
    // Wait (optionally until `deadline`) with the mutex released.
    void WaitImpl(Mutex::Guard& guard, Scheduler::TimePoint deadline);

    static Scheduler::TimePoint DeadlineAfter(Scheduler::Duration timeout);

    // Stored as tokens (fiber id + wait epoch) so stale entries — from Stop(),
    // fiber death, or a wait that already ended — are skipped on notify.
    detail::WaitQueue waiters_;
};

} // namespace cortex::tiny_fiber
