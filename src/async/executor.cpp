#include <cortex/async/executor.hpp>

#include <stdexcept>
#include <string>

namespace cortex::async {

struct Executor::Impl {
    std::string name;
    Config config;
};

Executor::Executor()
    : impl_(std::make_unique<Impl>()) {}

Executor::~Executor() = default;

std::string_view Executor::GetName() const noexcept {
    return impl_->name;
}

std::size_t Executor::GetThreadCount() const noexcept {
    return impl_->config.thread_count;
}

std::size_t Executor::GetDefaultStackSize() const noexcept {
    return impl_->config.default_stack_size;
}

SchedulerPolicy Executor::GetPolicy() const noexcept {
    return impl_->config.policy;
}

std::size_t Executor::GetActiveFiberCount() const noexcept {
    return 0; // Not implemented yet
}

std::size_t Executor::GetPendingFiberCount() const noexcept {
    return 0; // Not implemented yet
}

bool Executor::IsRunning() const noexcept {
    return false; // Not implemented yet
}

} // namespace cortex::async
