#pragma once

#include <cortex/tiny_fiber/detail/fiber.hpp>
#include <cortex/tiny_fiber/detail/wait_queue.hpp>

/**
 * @file mutex.hpp
 * @brief Cooperative mutex for tiny_fiber.
 */

namespace cortex::tiny_fiber {

/**
 * @class Mutex
 * @brief A cooperative mutex that yields instead of blocking.
 *
 * When a fiber tries to lock an already-locked mutex, it yields
 * control to other fibers until the mutex becomes available.
 */
class Mutex {
public:
    /**
     * @class Guard
     * @brief RAII lock guard for Mutex.
     */
    class Guard {
    public:
        /// Locks `mutex` (see Mutex::Lock()); the destructor unlocks it.
        explicit Guard(Mutex& mutex);
        ~Guard();

        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

        /// Takes over the lock; `other` no longer owns it.
        Guard(Guard&& other) noexcept;
        /// Releases the lock held, if any, and takes over `other`'s.
        Guard& operator=(Guard&& other) noexcept;

    private:
        friend class ConditionVariable;

        Mutex* mutex_ {nullptr};
    };

    Mutex() = default;
    ~Mutex();

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    /**
     * @brief Lock the mutex.
     *
     * If the mutex is already locked, the current fiber waits (other fibers
     * run) until it becomes available. Not a cancellation point: a
     * cancelled fiber still gets the lock, and sees the cancellation at its
     * next wait.
     *
     * @throws SchedulerStoppingError if the scheduler is stopping.
     * @throws std::logic_error outside of a fiber, or if this fiber already
     *         holds the mutex.
     */
    void Lock();

    /**
     * @brief Try to lock the mutex without yielding.
     *
     * @return true if the lock was acquired, false otherwise.
     */
    bool TryLock();

    /**
     * @brief Unlock the mutex.
     *
     * If there are fibers waiting for this mutex, one will be scheduled.
     */
    void Unlock();

    /**
     * @brief Check if the mutex is currently locked.
     */
    [[nodiscard]] bool IsLocked() const noexcept {
        return locked_;
    }

private:
    friend class ConditionVariable;

    // Waiters are stored as tokens (fiber id + wait epoch) rather than
    // pointers, so a fiber that died, or was woken by Scheduler::Stop() and
    // is now waiting on something else, is skipped on the next Unlock.
    bool locked_ {false};
    detail::Fiber* owner_ {nullptr};
    detail::WaitQueue waiters_;
};

/**
 * @brief Create a lock guard for the mutex.
 *
 * @param mutex The mutex to lock.
 * @return A Guard that will unlock the mutex on destruction.
 */
[[nodiscard]] inline Mutex::Guard Lock(Mutex& mutex) {
    return Mutex::Guard(mutex);
}

} // namespace cortex::tiny_fiber
