#include <cortex/tiny_fiber/detail/fiber.hpp>

#include <cassert>
#include <stdexcept>
#include <utility>

namespace cortex::tiny_fiber::detail {

Fiber::Fiber(Id id, Body body, std::size_t stack_size, MemoryResourceSharedPtr resource)
    : BaseCoroutine(stack_size, std::move(resource))
    , id_(id)
    , body_(std::move(body)) {}

void Fiber::Continuation(CoroutineSuspendContext& ctx) {
    suspend_ctx_ = &ctx;
    body_();
    suspend_ctx_ = nullptr;
}

void Fiber::Run() {
    assert(state_ == FiberState::Ready);
    state_ = FiberState::Running;
    Resume();
}

void Fiber::Yield() {
    assert(state_ == FiberState::Running);
    state_ = FiberState::Ready;
    Suspend();
}

void Fiber::Park() {
    assert(state_ == FiberState::Running);
    state_ = FiberState::Suspended;
    Suspend();
}

void Fiber::Wake() {
    assert(state_ == FiberState::Suspended);
    state_ = FiberState::Ready;
}

std::vector<Fiber::Id> Fiber::Complete() {
    suspend_ctx_ = nullptr;
    state_ = FiberState::Finished;
    return std::move(waiters_);
}

void Fiber::Suspend() {
    if (!suspend_ctx_) {
        throw std::logic_error("Cannot suspend: no suspend context available");
    }
    suspend_ctx_->Suspend();
}

void Fiber::AddWaiter(Id waiter_id) {
    if (waiter_id != 0) {
        waiters_.push_back(waiter_id);
    }
}

} // namespace cortex::tiny_fiber::detail
