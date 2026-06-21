#include <cortex/async/sync/mutex.hpp>

#include <stdexcept>

namespace cortex::async::sync {

struct Mutex::Impl {};

Mutex::Mutex()
    : impl_(std::make_unique<Impl>()) {}
Mutex::~Mutex() = default;

void Mutex::Lock() {
    throw std::runtime_error("Not implemented yet");
}

bool Mutex::TryLock() {
    throw std::runtime_error("Not implemented yet");
}

void Mutex::Unlock() {
    throw std::runtime_error("Not implemented yet");
}

bool Mutex::IsLocked() const noexcept {
    return false; // Not implemented yet
}

// Guard

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

// Free function

Mutex::Guard Lock(Mutex& mutex) {
    return Mutex::Guard(mutex);
}

} // namespace cortex::async::sync
