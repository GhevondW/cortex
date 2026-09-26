#pragma once

/**
 * @file condition_variable.hpp
 * @brief Fiber-aware ConditionVariable.
 */

#include <cortex/async/sync/mutex.hpp>

#include <chrono>
#include <memory>
#include <stdexcept>

namespace cortex::async::sync {

/**
 * @class ConditionVariable
 * @brief Fiber-aware condition variable.
 *
 * Suspends the current fiber until notified, releasing the associated
 * mutex while waiting. Re-acquires the mutex before returning.
 */
class ConditionVariable {
public:
    ConditionVariable();
    ~ConditionVariable();

    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;

    /**
     * @brief Suspend current fiber until notified.
     *
     * The mutex is released while waiting and re-acquired before returning.
     */
    void Wait(Mutex::Guard& guard);

    /**
     * @brief Wait with predicate (prevents spurious wakeups).
     */
    template <typename Predicate>
    void Wait(Mutex::Guard& guard, Predicate pred) {
        while (!pred()) {
            Wait(guard);
        }
    }

    /**
     * @brief Wait with timeout.
     * @return false if timed out.
     */
    template <typename Rep, typename Period>
    bool WaitFor([[maybe_unused]] Mutex::Guard& guard, [[maybe_unused]] std::chrono::duration<Rep, Period> timeout) {
        throw std::runtime_error("Not implemented yet");
    }

    /**
     * @brief Wait with timeout and predicate.
     * @return false if timed out before predicate became true.
     */
    template <typename Rep, typename Period, typename Predicate>
    bool WaitFor([[maybe_unused]] Mutex::Guard& guard,
                 [[maybe_unused]] std::chrono::duration<Rep, Period> timeout,
                 [[maybe_unused]] Predicate pred) {
        throw std::runtime_error("Not implemented yet");
    }

    /**
     * @brief Wake one waiting fiber.
     */
    void NotifyOne();

    /**
     * @brief Wake all waiting fibers.
     */
    void NotifyAll();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cortex::async::sync
