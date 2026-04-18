#include <cortex/async/sync/shared_mutex.hpp>

#include <stdexcept>

namespace cortex::async::sync {

struct SharedMutex::Impl {};

SharedMutex::SharedMutex()
    : impl_(std::make_unique<Impl>()) {}
SharedMutex::~SharedMutex() = default;

void SharedMutex::Lock() {
    throw std::runtime_error("Not implemented yet");
}

bool SharedMutex::TryLock() {
    throw std::runtime_error("Not implemented yet");
}

void SharedMutex::Unlock() {
    throw std::runtime_error("Not implemented yet");
}

void SharedMutex::LockShared() {
    throw std::runtime_error("Not implemented yet");
}

bool SharedMutex::TryLockShared() {
    throw std::runtime_error("Not implemented yet");
}

void SharedMutex::UnlockShared() {
    throw std::runtime_error("Not implemented yet");
}

// Guard

SharedMutex::Guard::Guard(SharedMutex& mutex)
    : mutex_(&mutex) {
    mutex_->Lock();
}

SharedMutex::Guard::~Guard() {
    if (mutex_) {
        mutex_->Unlock();
    }
}

SharedMutex::Guard::Guard(Guard&& other) noexcept
    : mutex_(other.mutex_) {
    other.mutex_ = nullptr;
}

SharedMutex::Guard& SharedMutex::Guard::operator=(Guard&& other) noexcept {
    if (this != &other) {
        if (mutex_) {
            mutex_->Unlock();
        }
        mutex_ = other.mutex_;
        other.mutex_ = nullptr;
    }
    return *this;
}

// SharedGuard

SharedMutex::SharedGuard::SharedGuard(SharedMutex& mutex)
    : mutex_(&mutex) {
    mutex_->LockShared();
}

SharedMutex::SharedGuard::~SharedGuard() {
    if (mutex_) {
        mutex_->UnlockShared();
    }
}

SharedMutex::SharedGuard::SharedGuard(SharedGuard&& other) noexcept
    : mutex_(other.mutex_) {
    other.mutex_ = nullptr;
}

SharedMutex::SharedGuard& SharedMutex::SharedGuard::operator=(SharedGuard&& other) noexcept {
    if (this != &other) {
        if (mutex_) {
            mutex_->UnlockShared();
        }
        mutex_ = other.mutex_;
        other.mutex_ = nullptr;
    }
    return *this;
}

// Free functions

SharedMutex::Guard LockExclusive(SharedMutex& mutex) {
    return SharedMutex::Guard(mutex);
}

SharedMutex::SharedGuard LockShared(SharedMutex& mutex) {
    return SharedMutex::SharedGuard(mutex);
}

} // namespace cortex::async::sync
