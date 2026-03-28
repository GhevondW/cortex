#pragma once

#include <cortex/fiber/detail/platform.hpp>
#include <cortex/fiber/mutex.hpp>

#include <atomic>
#include <cstdint>

namespace cortex::fiber {

class ConditionVariable {
public:
    ConditionVariable() = default;
    ~ConditionVariable() = default;

    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;

    void Wait(Mutex::Guard& guard);

    template <typename Predicate>
    void Wait(Mutex::Guard& guard, Predicate pred) {
        while (!pred()) {
            Wait(guard);
        }
    }

    void NotifyOne() noexcept;
    void NotifyAll() noexcept;

private:
    std::atomic<std::uint64_t> next_ticket_ {0};
    std::atomic<std::uint64_t> signaled_ticket_ {0};
};

} // namespace cortex::fiber
