#include <cortex/tiny_fiber/detail/fiber.hpp>

#include <cassert>
#include <stdexcept>
#include <utility>

namespace cortex::tiny_fiber::detail {

Fiber::Fiber(Id id, Body body, std::size_t stack_size, MemoryResourceSharedPtr resource, bool reusable)
    : BaseCoroutine(stack_size, std::move(resource), reusable)
    , id_(id)
    , body_(std::move(body)) {}

void Fiber::ResetForReuse(Id id, Body body) {
    assert(state_ == FiberState::Finished);
    id_ = id;
    body_ = std::move(body);
    state_ = FiberState::Ready;
    suspend_ctx_ = nullptr;
    inline_waiter_count_ = 0;
    overflow_waiters_.clear();
    ResetCoroutineForReuse();
}

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

void Fiber::Complete() {
    suspend_ctx_ = nullptr;
    state_ = FiberState::Finished;
}

void Fiber::Suspend() {
    if (!suspend_ctx_) {
        throw std::logic_error("Cannot suspend: no suspend context available");
    }
    suspend_ctx_->Suspend();
}

void Fiber::AddWaiter(Id waiter_id) {
    if (waiter_id == 0) {
        return;
    }

    if (inline_waiter_count_ < inline_waiters_.size()) {
        inline_waiters_[inline_waiter_count_] = waiter_id;
        ++inline_waiter_count_;
    } else {
        overflow_waiters_.push_back(waiter_id);
    }
}

} // namespace cortex::tiny_fiber::detail
