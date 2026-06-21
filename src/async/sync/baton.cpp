#include <cortex/async/sync/baton.hpp>

#include <stdexcept>

namespace cortex::async::sync {

struct Baton::Impl {};

Baton::Baton()
    : impl_(std::make_unique<Impl>()) {}
Baton::~Baton() = default;

void Baton::Wait() {
    throw std::runtime_error("Not implemented yet");
}

void Baton::Post() {
    throw std::runtime_error("Not implemented yet");
}

bool Baton::IsPosted() const noexcept {
    return false; // Not implemented yet
}

void Baton::Reset() {
    throw std::runtime_error("Not implemented yet");
}

} // namespace cortex::async::sync
