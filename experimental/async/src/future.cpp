#include <cortex/async/future.hpp>

#include <stdexcept>

namespace cortex::async {

// --- Promise<void> ---

struct Promise<void>::SharedState {};

Promise<void>::Promise()
    : state_(std::make_shared<SharedState>()) {}
Promise<void>::~Promise() = default;

Promise<void>::Promise(Promise&&) noexcept = default;
Promise<void>& Promise<void>::operator=(Promise&&) noexcept = default;

Future<void> Promise<void>::GetFuture() {
    throw std::runtime_error("Not implemented yet");
}

void Promise<void>::SetValue() {
    throw std::runtime_error("Not implemented yet");
}

void Promise<void>::SetException([[maybe_unused]] std::exception_ptr ex) {
    throw std::runtime_error("Not implemented yet");
}

bool Promise<void>::IsFulfilled() const noexcept {
    return false; // Not implemented yet
}

// --- Future<void> ---

struct Future<void>::SharedState {};

Future<void>::Future(Future&&) noexcept = default;
Future<void>& Future<void>::operator=(Future&&) noexcept = default;
Future<void>::~Future() = default;

Future<void>::Future(std::shared_ptr<SharedState> state)
    : state_(std::move(state)) {}

void Future<void>::Wait() {
    throw std::runtime_error("Not implemented yet");
}

void Future<void>::Get() {
    throw std::runtime_error("Not implemented yet");
}

bool Future<void>::IsReady() const noexcept {
    return false; // Not implemented yet
}

} // namespace cortex::async
