#include <cortex/async/runtime.hpp>

#include <stdexcept>

namespace cortex::async {

struct Runtime::Impl {
    Config config;
};

std::unique_ptr<Runtime> Runtime::Create() {
    return Create(Config {});
}

std::unique_ptr<Runtime> Runtime::Create([[maybe_unused]] Config config) {
    throw std::runtime_error("Not implemented yet");
}

Runtime::Runtime(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

Runtime::~Runtime() = default;

Executor& Runtime::GetDefaultExecutor() noexcept {
    std::abort(); // Not implemented yet
}

Executor* Runtime::GetExecutor([[maybe_unused]] std::string_view name) noexcept {
    return nullptr; // Not implemented yet
}

Executor& Runtime::CreateExecutor([[maybe_unused]] std::string_view name, [[maybe_unused]] std::size_t thread_count) {
    throw std::runtime_error("Not implemented yet");
}

std::size_t Runtime::GetTotalThreadCount() const noexcept {
    return 0; // Not implemented yet
}

const Runtime::Config& Runtime::GetConfig() const noexcept {
    return impl_->config;
}

void Runtime::Shutdown() {
    throw std::runtime_error("Not implemented yet");
}

bool Runtime::IsShuttingDown() const noexcept {
    return false; // Not implemented yet
}

void Runtime::WaitForCompletion() {
    throw std::runtime_error("Not implemented yet");
}

} // namespace cortex::async
