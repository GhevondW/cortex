#pragma once

/**
 * @file shared_mutex.hpp
 * @brief Fiber-aware SharedMutex (reader-writer lock).
 */

#include <memory>

namespace cortex::async::sync {

/**
 * @class SharedMutex
 * @brief Fiber-aware reader-writer lock.
 *
 * Multiple fibers can hold a shared (read) lock simultaneously.
 * Only one fiber can hold an exclusive (write) lock.
 * Fibers suspend instead of blocking the OS thread.
 */
class SharedMutex {
public:
    /**
     * @class Guard
     * @brief RAII exclusive lock guard.
     */
    class Guard {
    public:
        explicit Guard(SharedMutex& mutex);
        ~Guard();
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
        Guard(Guard&&) noexcept;
        Guard& operator=(Guard&&) noexcept;

    private:
        SharedMutex* mutex_ {nullptr};
    };

    /**
     * @class SharedGuard
     * @brief RAII shared (read) lock guard.
     */
    class SharedGuard {
    public:
        explicit SharedGuard(SharedMutex& mutex);
        ~SharedGuard();
        SharedGuard(const SharedGuard&) = delete;
        SharedGuard& operator=(const SharedGuard&) = delete;
        SharedGuard(SharedGuard&&) noexcept;
        SharedGuard& operator=(SharedGuard&&) noexcept;

    private:
        SharedMutex* mutex_ {nullptr};
    };

    SharedMutex();
    ~SharedMutex();

    SharedMutex(const SharedMutex&) = delete;
    SharedMutex& operator=(const SharedMutex&) = delete;

    void Lock();
    [[nodiscard]] bool TryLock();
    void Unlock();

    void LockShared();
    [[nodiscard]] bool TryLockShared();
    void UnlockShared();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Acquire an exclusive lock.
 */
[[nodiscard]] SharedMutex::Guard LockExclusive(SharedMutex& mutex);

/**
 * @brief Acquire a shared (read) lock.
 */
[[nodiscard]] SharedMutex::SharedGuard LockShared(SharedMutex& mutex);

} // namespace cortex::async::sync
