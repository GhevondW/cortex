#pragma once

/**
 * @file wait_group.hpp
 * @brief Go-style WaitGroup for waiting on N completions.
 */

#include <cstddef>
#include <memory>

namespace cortex::async::sync {

/**
 * @class WaitGroup
 * @brief Wait for N tasks to complete.
 *
 * Inspired by Go's sync.WaitGroup. Call Add() before spawning work,
 * Done() when each piece of work completes, and Wait() to suspend
 * until the counter reaches zero.
 */
class WaitGroup {
public:
    WaitGroup();
    ~WaitGroup();

    WaitGroup(const WaitGroup&) = delete;
    WaitGroup& operator=(const WaitGroup&) = delete;

    /**
     * @brief Increment the counter by delta.
     */
    void Add(std::size_t delta = 1);

    /**
     * @brief Decrement the counter by 1.
     *
     * When the counter reaches 0, all fibers blocked on Wait() are woken.
     */
    void Done();

    /**
     * @brief Suspend current fiber until the counter reaches 0.
     */
    void Wait();

    /**
     * @brief Current counter value.
     */
    [[nodiscard]] std::size_t GetCount() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cortex::async::sync
