#pragma once

#include <cortex/tiny_fiber/detail/fiber.hpp>

#include <deque>

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
        explicit Guard(Mutex& mutex);
        ~Guard();

        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

        Guard(Guard&& other) noexcept;
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
     * If the mutex is already locked, the current fiber yields
     * until it becomes available.
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

    bool locked_ {false};
    detail::Fiber* owner_ {nullptr};
    std::deque<detail::Fiber*> waiters_;
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
