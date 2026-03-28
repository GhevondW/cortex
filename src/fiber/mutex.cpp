#include <cortex/fiber/errors/scheduler_stopping_error.hpp>
#include <cortex/fiber/mutex.hpp>
#include <cortex/fiber/scheduler.hpp>

#include <stdexcept>

namespace cortex::fiber {

void Mutex::Lock() {
    auto& scheduler = Scheduler::Current();
    auto* current = scheduler.GetCurrentFiber();
    if (!current) {
        throw std::logic_error("Mutex::Lock() must be called from within a fiber");
    }

    const auto current_id = current->GetId();
    if (owner_.load(std::memory_order_acquire) == current_id) {
        throw std::logic_error("Mutex::Lock() recursive lock detected");
    }

    while (true) {
        if (scheduler.IsStopping()) {
            throw SchedulerStoppingError();
        }

        bool expected = false;
        if (locked_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            owner_.store(current_id, std::memory_order_release);
            return;
        }

        scheduler.YieldCurrent();
    }
}

bool Mutex::TryLock() {
    auto& scheduler = Scheduler::Current();
    auto* current = scheduler.GetCurrentFiber();
    if (!current) {
        throw std::logic_error("Mutex::TryLock() must be called from within a fiber");
    }

    bool expected = false;
    if (locked_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        owner_.store(current->GetId(), std::memory_order_release);
        return true;
    }

    return false;
}

void Mutex::Unlock() {
    auto& scheduler = Scheduler::Current();
    auto* current = scheduler.GetCurrentFiber();
    if (!current) {
        throw std::logic_error("Mutex::Unlock() must be called from within a fiber");
    }

    if (!locked_.load(std::memory_order_acquire)) {
        throw std::logic_error("Mutex::Unlock() called on unlocked mutex");
    }

    const auto owner = owner_.load(std::memory_order_acquire);
    if (owner != current->GetId()) {
        throw std::logic_error("Mutex::Unlock() called by non-owner fiber");
    }

    owner_.store(0, std::memory_order_release);
    locked_.store(false, std::memory_order_release);
}

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

} // namespace cortex::fiber
