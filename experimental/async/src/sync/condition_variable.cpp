#include <cortex/async/sync/condition_variable.hpp>

#include <stdexcept>

namespace cortex::async::sync {

struct ConditionVariable::Impl {};

ConditionVariable::ConditionVariable()
    : impl_(std::make_unique<Impl>()) {}
ConditionVariable::~ConditionVariable() = default;

void ConditionVariable::Wait([[maybe_unused]] Mutex::Guard& guard) {
    throw std::runtime_error("Not implemented yet");
}

void ConditionVariable::NotifyOne() {
    throw std::runtime_error("Not implemented yet");
}

void ConditionVariable::NotifyAll() {
    throw std::runtime_error("Not implemented yet");
}

} // namespace cortex::async::sync
