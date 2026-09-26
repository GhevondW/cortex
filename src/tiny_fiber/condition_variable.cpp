#include <cortex/tiny_fiber/condition_variable.hpp>
#include <cortex/tiny_fiber/errors/scheduler_stopping_error.hpp>
#include <cortex/tiny_fiber/scheduler.hpp>

#include <stdexcept>

namespace cortex::tiny_fiber {

ConditionVariable::~ConditionVariable() = default;

void ConditionVariable::Wait(Mutex::Guard& guard) {
    WaitImpl(guard, Scheduler::TimePoint::max());
}

bool ConditionVariable::WaitUntil(Mutex::Guard& guard, Scheduler::TimePoint deadline) {
    WaitImpl(guard, deadline);
    return Scheduler::Current().Now() < deadline;
}

Scheduler::TimePoint ConditionVariable::DeadlineAfter(Scheduler::Duration timeout) {
    const auto now = Scheduler::Current().Now();
    return timeout >= Scheduler::TimePoint::max() - now ? Scheduler::TimePoint::max() : now + timeout;
}

void ConditionVariable::WaitImpl(Mutex::Guard& guard, Scheduler::TimePoint deadline) {
    if (!guard.mutex_) {
        throw std::logic_error("ConditionVariable::Wait() called with invalid guard");
    }

    auto& scheduler = Scheduler::Current();

    // A cancellation point: stopping or a cancelled fiber throws here.
    scheduler.ThrowIfInterrupted(true);

    auto* current = scheduler.GetCurrentFiber();
    if (!current) {
        throw std::logic_error("ConditionVariable::Wait() must be called from within a fiber");
    }

    waiters_.Push(scheduler.PrepareWait(), scheduler);

    // Detach the guard from the mutex before unlocking so that, if any subsequent
    // step throws, the Guard destructor doesn't try to Unlock an unlocked mutex
    // during stack unwinding (which would terminate via throw-in-destructor).
    auto* mutex = guard.mutex_;
    guard.mutex_ = nullptr;
    mutex->Unlock();

    // Woken by a notify, the deadline, Stop() or Cancel(). A deadline wake
    // leaves this wait's token in waiters_; its epoch keeps it from ever
    // waking a later wait.
    scheduler.ParkCurrentUntil(
        deadline == Scheduler::TimePoint::max() ? "ConditionVariable::Wait" : "ConditionVariable::WaitFor",
        true,
        deadline);

    // Stopped or cancelled: leave without the mutex (the guard is detached, so
    // its destructor will not unlock). If a NotifyOne() picked this fiber, the
    // notification would be lost with it: wake the next waiter instead (a
    // spurious wake-up at worst, which condition-variable users handle).
    try {
        scheduler.ThrowIfInterrupted(true);
    } catch (...) {
        waiters_.WakeOne(scheduler);
        throw;
    }

    mutex->Lock();
    guard.mutex_ = mutex; // Re-attach: guard owns the mutex again.
}

void ConditionVariable::NotifyOne() {
    waiters_.WakeOne(Scheduler::Current());
}

void ConditionVariable::NotifyAll() {
    waiters_.WakeAll(Scheduler::Current());
}

} // namespace cortex::tiny_fiber
