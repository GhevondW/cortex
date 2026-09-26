#pragma once

/**
 * @file mutex.hpp
 * @brief Fiber-aware Mutex with RAII Guard.
 */

#include <memory>

namespace cortex::async::sync {

/**
 * @class Mutex
 * @brief Fiber-aware mutual exclusion lock.
 *
 * When a fiber tries to lock an already-held mutex, it suspends
 * (yields to the scheduler) instead of blocking the OS thread.
 *
 * Non-copyable, non-movable.
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

    Mutex();
    ~Mutex();

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    /**
     * @brief Lock the mutex. Suspends the current fiber if already held.
     */
    void Lock();

    /**
     * @brief Try to acquire without suspending.
     * @return true if acquired.
     */
    [[nodiscard]] bool TryLock();

    /**
     * @brief Unlock the mutex. Wakes one waiting fiber if any.
     */
    void Unlock();

    /**
     * @brief Check if the mutex is currently locked.
     */
    [[nodiscard]] bool IsLocked() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Free function for RAII lock acquisition.
 */
[[nodiscard]] Mutex::Guard Lock(Mutex& mutex);

} // namespace cortex::async::sync
