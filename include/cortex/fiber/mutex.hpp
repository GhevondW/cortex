#pragma once

#include <cortex/fiber/detail/fiber.hpp>
#include <cortex/fiber/detail/platform.hpp>

#include <atomic>

namespace cortex::fiber {

class ConditionVariable;

class Mutex {
public:
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
    ~Mutex() = default;

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    void Lock();
    bool TryLock();
    void Unlock();

    [[nodiscard]] bool IsLocked() const noexcept {
        return locked_.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> locked_ {false};
    std::atomic<detail::Fiber::Id> owner_ {0};
};

[[nodiscard]] inline Mutex::Guard Lock(Mutex& mutex) {
    return Mutex::Guard(mutex);
}

} // namespace cortex::fiber
