#pragma once

#include <cortex/tiny_fiber/detail/fiber.hpp>
#include <cortex/tiny_fiber/mutex.hpp>

#include <deque>

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
     * @param guard The lock guard holding the mutex.
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
     * @brief Wake one waiting fiber.
     */
    void NotifyOne();

    /**
     * @brief Wake all waiting fibers.
     */
    void NotifyAll();

private:
    // Stored by ID, not pointer, so stale entries (from Stop() or fiber death)
    // can be safely detected and skipped on notify.
    std::deque<detail::Fiber::Id> waiters_;
};

} // namespace cortex::tiny_fiber
