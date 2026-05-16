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

    waiters_.push_back(current->GetId());

    // Detach the guard from the mutex before unlocking so that, if any subsequent
    // step throws, the Guard destructor doesn't try to Unlock an unlocked mutex
    // during stack unwinding (which would terminate via throw-in-destructor).
    auto* mutex = guard.mutex_;
    guard.mutex_ = nullptr;
    mutex->Unlock();

    scheduler.SuspendCurrent();

    if (scheduler.IsStopping()) {
        throw SchedulerStoppingError();
    }

    mutex->Lock();
    guard.mutex_ = mutex; // Re-attach: guard owns the mutex again.
}

void ConditionVariable::NotifyOne() {
    auto& scheduler = Scheduler::Current();
    // Skip stale entries (fibers that died or were force-scheduled).
    while (!waiters_.empty()) {
        auto id = waiters_.front();
        waiters_.pop_front();
        auto* waiter = scheduler.GetFiber(id);
        if (waiter && waiter->IsSuspended()) {
            scheduler.Schedule(waiter);
            return;
        }
    }
}

void ConditionVariable::NotifyAll() {
    auto& scheduler = Scheduler::Current();
    while (!waiters_.empty()) {
        auto id = waiters_.front();
        waiters_.pop_front();
        auto* waiter = scheduler.GetFiber(id);
        if (waiter && waiter->IsSuspended()) {
            scheduler.Schedule(waiter);
        }
    }
}

} // namespace cortex::tiny_fiber
