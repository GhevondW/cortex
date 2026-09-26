#pragma once

/**
 * @file event.hpp
 * @brief Fiber-aware event with manual or automatic reset.
 */

#include <cstdint>
#include <memory>

namespace cortex::async::sync {

/**
 * @enum EventResetPolicy
 * @brief Controls how the event resets after being signaled.
 */
enum class EventResetPolicy : std::uint8_t {
    kManual, ///< Stays signaled until Reset() is called
    kAutomatic ///< Resets after one waiter is released
};

/**
 * @class Event
 * @brief Fiber-aware event synchronization primitive.
 *
 * Manual-reset: Signal() wakes all waiters and stays signaled.
 * Auto-reset: Signal() wakes one waiter and resets.
 */
class Event {
public:
    explicit Event(EventResetPolicy policy = EventResetPolicy::kManual);
    ~Event();

    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;

    /**
     * @brief Suspend current fiber until the event is signaled.
     */
    void Wait();

    /**
     * @brief Signal the event.
     *
     * Manual: wakes all waiters and stays signaled.
     * Auto: wakes one waiter and resets.
     */
    void Signal();

    /**
     * @brief Reset to unsignaled state (manual-reset events only).
     */
    void Reset();

    /**
     * @brief Check if the event is currently signaled.
     */
    [[nodiscard]] bool IsSignaled() const noexcept;

    /**
     * @brief Get the reset policy.
     */
    [[nodiscard]] EventResetPolicy GetPolicy() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cortex::async::sync
