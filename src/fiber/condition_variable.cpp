#include <cortex/fiber/condition_variable.hpp>
#include <cortex/fiber/errors/scheduler_stopping_error.hpp>
#include <cortex/fiber/scheduler.hpp>

#include <stdexcept>

namespace cortex::fiber {

void ConditionVariable::Wait(Mutex::Guard& guard) {
    if (!guard.mutex_) {
        throw std::logic_error("ConditionVariable::Wait() called with invalid guard");
    }

    auto& scheduler = Scheduler::Current();
    if (scheduler.IsStopping()) {
        throw SchedulerStoppingError();
    }

    const auto ticket = next_ticket_.fetch_add(1, std::memory_order_acq_rel);
    guard.mutex_->Unlock();

    while (signaled_ticket_.load(std::memory_order_acquire) <= ticket) {
        if (scheduler.IsStopping()) {
            // Wait unlocks the mutex before suspension. If we abort due stop before
            // re-locking, invalidate the guard so its destructor does not double-unlock.
            guard.mutex_ = nullptr;
            throw SchedulerStoppingError();
        }
        scheduler.YieldCurrent();
    }

    guard.mutex_->Lock();
}

void ConditionVariable::NotifyOne() noexcept {
    auto signaled = signaled_ticket_.load(std::memory_order_acquire);
    const auto waiting = next_ticket_.load(std::memory_order_acquire);

    while (signaled < waiting) {
        if (signaled_ticket_.compare_exchange_weak(signaled, signaled + 1, std::memory_order_acq_rel)) {
            return;
        }
    }
}

void ConditionVariable::NotifyAll() noexcept {
    const auto waiting = next_ticket_.load(std::memory_order_acquire);
    signaled_ticket_.store(waiting, std::memory_order_release);
}

} // namespace cortex::fiber
