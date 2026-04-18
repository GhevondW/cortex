#pragma once

/**
 * @file baton.hpp
 * @brief Lightweight single-use synchronization (Folly-style).
 */

#include <chrono>
#include <memory>
#include <stdexcept>

namespace cortex::async::sync {

/**
 * @class Baton
 * @brief Lightweight single-use synchronization primitive.
 *
 * Inspired by folly::fibers::Baton. One fiber waits, another posts.
 * Once posted, the baton stays posted until Reset() is called.
 */
class Baton {
public:
    Baton();
    ~Baton();

    Baton(const Baton&) = delete;
    Baton& operator=(const Baton&) = delete;

    /**
     * @brief Suspend current fiber until Post() is called.
     *
     * If already posted, returns immediately.
     */
    void Wait();

    /**
     * @brief Wait with timeout.
     * @return true if posted, false if timed out.
     */
    template <typename Rep, typename Period>
    bool WaitFor([[maybe_unused]] std::chrono::duration<Rep, Period> timeout) {
        throw std::runtime_error("Not implemented yet");
    }

    /**
     * @brief Signal the baton. Wakes the waiting fiber (if any).
     *
     * Can be called from any fiber or thread.
     */
    void Post();

    /**
     * @brief Check if posted (non-blocking).
     */
    [[nodiscard]] bool IsPosted() const noexcept;

    /**
     * @brief Reset for reuse. Must not be called while a fiber is waiting.
     */
    void Reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cortex::async::sync
