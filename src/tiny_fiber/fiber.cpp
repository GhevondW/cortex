#include <cortex/tiny_fiber/detail/fiber.hpp>

#include <cassert>
#include <stdexcept>

namespace cortex::tiny_fiber::detail {

Fiber::Fiber(Id id, Coroutine coroutine, PrivateTag)
    : id_(id)
    , coroutine_(std::move(coroutine)) {}

void Fiber::Resume() {
    coroutine_.Resume();
}

void Fiber::Suspend() {
    if (!suspend_ctx_) {
        throw std::logic_error("Cannot suspend: no suspend context available");
    }
    suspend_ctx_->Suspend();
}

void Fiber::AddWaiter(Fiber* waiter) {
    if (waiter) {
        waiters_.push_back(waiter);
    }
}

std::deque<Fiber*> Fiber::TakeWaiters() {
    return std::move(waiters_);
}

} // namespace cortex::tiny_fiber::detail
