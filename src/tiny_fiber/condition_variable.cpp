#include <cortex/tiny_fiber/condition_variable.hpp>
#include <cortex/tiny_fiber/errors/scheduler_stopping_error.hpp>
#include <cortex/tiny_fiber/scheduler.hpp>

#include <cassert>
#include <stdexcept>

namespace cortex::tiny_fiber {

ConditionVariable::~ConditionVariable() {
    // In debug mode, this indicates a programming error
    // We handle it gracefully by clearing waiters
#ifndef NDEBUG
    if (!waiters_.empty()) {
        // ConditionVariable destroyed with waiters - this is a bug but don't crash
    }
#endif
    // Clear waiters to avoid dangling pointers
    waiters_.clear();
}

void ConditionVariable::Wait(Mutex::Guard& guard) {
    if (!guard.mutex_) {
        throw std::logic_error("ConditionVariable::Wait() called with invalid guard");
    }

    auto& scheduler = Scheduler::Current();

    // Check for stopping
    if (scheduler.IsStopping()) {
        throw SchedulerStoppingError();
    }

    auto* current = scheduler.GetCurrentFiber();

    if (!current) {
        throw std::logic_error("ConditionVariable::Wait() must be called from within a fiber");
    }

    // Add to wait queue
    waiters_.push_back(current);

    // Unlock mutex while waiting
    guard.mutex_->Unlock();

    // Suspend until notified
    scheduler.SuspendCurrent();

    // Check for stopping after waking up
    if (scheduler.IsStopping()) {
        // Don't try to re-lock, just propagate the stop signal
        throw SchedulerStoppingError();
    }

    // Re-lock mutex after waking up
    guard.mutex_->Lock();
}

void ConditionVariable::NotifyOne() {
    if (waiters_.empty()) {
        return;
    }

    auto& scheduler = Scheduler::Current();
    auto* waiter = waiters_.front();
    waiters_.pop_front();
    scheduler.Schedule(waiter);
}

void ConditionVariable::NotifyAll() {
    if (waiters_.empty()) {
        return;
    }

    auto& scheduler = Scheduler::Current();
    while (!waiters_.empty()) {
        auto* waiter = waiters_.front();
        waiters_.pop_front();
        scheduler.Schedule(waiter);
    }
}

} // namespace cortex::tiny_fiber
