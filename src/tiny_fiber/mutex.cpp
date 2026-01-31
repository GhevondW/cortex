#include <cortex/tiny_fiber/mutex.hpp>
#include <cortex/tiny_fiber/scheduler.hpp>

#include <cassert>
#include <stdexcept>

namespace cortex::tiny_fiber {

Mutex::~Mutex() {
    // Mutex should not be destroyed while locked
    assert(!locked_);
    assert(waiters_.empty());
}

void Mutex::Lock() {
    auto& scheduler = Scheduler::Current();
    auto* current = scheduler.GetCurrentFiber();

    if (!current) {
        throw std::logic_error("Mutex::Lock() must be called from within a fiber");
    }

    // Check for recursive lock (same fiber trying to lock twice)
    if (locked_ && owner_ == current) {
        throw std::logic_error("Mutex::Lock() recursive lock detected");
    }

    while (locked_) {
        // Add to wait queue
        waiters_.push_back(current);
        // Suspend until mutex is available
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

    if (owner_ != current) {
        throw std::logic_error("Mutex::Unlock() called by non-owner fiber");
    }

    locked_ = false;
    owner_ = nullptr;

    // Wake up one waiter if any
    if (!waiters_.empty()) {
        auto* waiter = waiters_.front();
        waiters_.pop_front();
        scheduler.Schedule(waiter);
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
