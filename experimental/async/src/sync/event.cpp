#include <cortex/async/sync/event.hpp>

#include <stdexcept>

namespace cortex::async::sync {

struct Event::Impl {
    EventResetPolicy policy;
};

Event::Event(EventResetPolicy policy)
    : impl_(std::make_unique<Impl>(Impl {policy})) {}
Event::~Event() = default;

void Event::Wait() {
    throw std::runtime_error("Not implemented yet");
}

void Event::Signal() {
    throw std::runtime_error("Not implemented yet");
}

void Event::Reset() {
    throw std::runtime_error("Not implemented yet");
}

bool Event::IsSignaled() const noexcept {
    return false; // Not implemented yet
}

EventResetPolicy Event::GetPolicy() const noexcept {
    return impl_->policy;
}

} // namespace cortex::async::sync
