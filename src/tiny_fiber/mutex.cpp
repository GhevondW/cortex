#include <cortex/tiny_fiber/errors/scheduler_stopping_error.hpp>
#include <cortex/tiny_fiber/mutex.hpp>
#include <cortex/tiny_fiber/scheduler.hpp>

#include <stdexcept>

namespace cortex::tiny_fiber {

Mutex::~Mutex() = default;

void Mutex::Lock() {
    auto& scheduler = Scheduler::Current();

    if (scheduler.IsStopping()) {
        throw SchedulerStoppingError();
    }

    auto* current = scheduler.GetCurrentFiber();
    if (!current) {
        throw std::logic_error("Mutex::Lock() must be called from within a fiber");
    }

    if (locked_ && owner_ == current) {
        throw std::logic_error("Mutex::Lock() recursive lock detected");
    }

    while (locked_) {
        if (scheduler.IsStopping()) {
            // We may have a stale entry in waiters_ from a previous iteration.
            // Unlock() validates entries on pop, so we leave it for cleanup there.
            throw SchedulerStoppingError();
        }
        waiters_.push_back(current->GetId());
        scheduler.SuspendCurrent();
    }

    locked_ = true;
    owner_ = current;
}

bool Mutex::TryLock() {
    if (locked_) {
        return false;
    }

    auto& scheduler = Scheduler::Current();
    auto* current = scheduler.GetCurrentFiber();

    locked_ = true;
    owner_ = current;
    return true;
}

void Mutex::Unlock() {
    if (!locked_) {
        throw std::logic_error("Mutex::Unlock() called on unlocked mutex");
    }

    auto& scheduler = Scheduler::Current();
    auto* current = scheduler.GetCurrentFiber();

    // During scheduler destruction current may be nullptr; skip the owner check.
    if (current != nullptr && owner_ != current) {
        throw std::logic_error("Mutex::Unlock() called by non-owner fiber");
    }

    locked_ = false;
    owner_ = nullptr;

    if (current == nullptr) {
        return;
    }

    // Pop until we find a still-Suspended waiter. Entries can be stale if the
    // fiber was force-scheduled by Stop() or has already completed.
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

// Guard implementation

Mutex::Guard::Guard(Mutex& mutex)
    : mutex_(&mutex) {
    mutex_->Lock();
}

Mutex::Guard::~Guard() {
    if (mutex_) {
        mutex_->Unlock();
    }
}

Mutex::Guard::Guard(Guard&& other) noexcept
    : mutex_(other.mutex_) {
    other.mutex_ = nullptr;
}

Mutex::Guard& Mutex::Guard::operator=(Guard&& other) noexcept {
    if (this != &other) {
        if (mutex_) {
            mutex_->Unlock();
        }
        mutex_ = other.mutex_;
        other.mutex_ = nullptr;
    }
    return *this;
}

} // namespace cortex::tiny_fiber
