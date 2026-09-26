#include <cortex/async/sync/semaphore.hpp>

#include <stdexcept>

namespace cortex::async::sync {

struct Semaphore::Impl {};

Semaphore::Semaphore([[maybe_unused]] std::size_t initial_count)
    : impl_(std::make_unique<Impl>()) {}
Semaphore::~Semaphore() = default;

void Semaphore::Acquire() {
    throw std::runtime_error("Not implemented yet");
}

bool Semaphore::TryAcquire() {
    throw std::runtime_error("Not implemented yet");
}

void Semaphore::Release([[maybe_unused]] std::size_t count) {
    throw std::runtime_error("Not implemented yet");
}

std::size_t Semaphore::GetCount() const noexcept {
    return 0; // Not implemented yet
}

} // namespace cortex::async::sync
