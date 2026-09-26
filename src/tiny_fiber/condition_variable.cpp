#include <cortex/tiny_fiber/condition_variable.hpp>
#include <cortex/tiny_fiber/errors/scheduler_stopping_error.hpp>
#include <cortex/tiny_fiber/scheduler.hpp>

#include <stdexcept>

namespace cortex::tiny_fiber {

ConditionVariable::~ConditionVariable() = default;

void ConditionVariable::Wait(Mutex::Guard& guard) {
    if (!guard.mutex_) {
        throw std::logic_error("ConditionVariable::Wait() called with invalid guard");
    }

    auto& scheduler = Scheduler::Current();

    if (scheduler.IsStopping()) {
        throw SchedulerStoppingError();
    }

    auto* current = scheduler.GetCurrentFiber();
    if (!current) {
        throw std::logic_error("ConditionVariable::Wait() must be called from within a fiber");
    }

    waiters_.Push(scheduler.PrepareWait());

    // Detach the guard from the mutex before unlocking so that, if any subsequent
    // step throws, the Guard destructor doesn't try to Unlock an unlocked mutex
    // during stack unwinding (which would terminate via throw-in-destructor).
    auto* mutex = guard.mutex_;
    guard.mutex_ = nullptr;
    mutex->Unlock();

    scheduler.ParkCurrent("ConditionVariable::Wait", true);

    if (scheduler.IsStopping()) {
        throw SchedulerStoppingError();
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
