#pragma once

/**
 * @file semaphore.hpp
 * @brief Fiber-aware counting semaphore.
 */

#include <chrono>
#include <cstddef>
#include <memory>
#include <stdexcept>

namespace cortex::async::sync {

/**
 * @class Semaphore
 * @brief Fiber-aware counting semaphore.
 *
 * Fibers that call Acquire() when the count is zero are suspended
 * until another fiber calls Release().
 */
class Semaphore {
public:
    explicit Semaphore(std::size_t initial_count = 0);
    ~Semaphore();

    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;

    /**
     * @brief Decrement count. Suspends if count is 0.
     */
    void Acquire();

    /**
     * @brief Try to decrement without suspending.
     */
    [[nodiscard]] bool TryAcquire();

    /**
     * @brief Try with timeout.
     * @return true if acquired, false if timed out.
     */
    template <typename Rep, typename Period>
    bool TryAcquireFor([[maybe_unused]] std::chrono::duration<Rep, Period> timeout) {
        throw std::runtime_error("Not implemented yet");
    }

    /**
     * @brief Increment count. Wakes one waiting fiber if any.
     */
    void Release(std::size_t count = 1);

    /**
     * @brief Current count.
     */
    [[nodiscard]] std::size_t GetCount() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cortex::async::sync
