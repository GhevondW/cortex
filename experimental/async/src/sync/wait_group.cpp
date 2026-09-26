#include <cortex/async/sync/wait_group.hpp>

#include <stdexcept>

namespace cortex::async::sync {

struct WaitGroup::Impl {};

WaitGroup::WaitGroup()
    : impl_(std::make_unique<Impl>()) {}
WaitGroup::~WaitGroup() = default;

void WaitGroup::Add([[maybe_unused]] std::size_t delta) {
    throw std::runtime_error("Not implemented yet");
}

void WaitGroup::Done() {
    throw std::runtime_error("Not implemented yet");
}

void WaitGroup::Wait() {
    throw std::runtime_error("Not implemented yet");
}

std::size_t WaitGroup::GetCount() const noexcept {
    return 0; // Not implemented yet
}

} // namespace cortex::async::sync
