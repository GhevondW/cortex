#include <cortex/fiber/detail/fiber.hpp>

#include <atomic>
#include <cassert>
#include <stdexcept>
#include <utility>

namespace cortex::fiber::detail {

namespace {
std::atomic<Fiber::Id> g_next_fiber_id {1};
} // namespace

std::unique_ptr<Fiber> Fiber::Make(Body body, std::size_t stack_size, MemoryResourceSharedPtr resource) {
    const auto id = g_next_fiber_id.fetch_add(1, std::memory_order_relaxed);
    return std::make_unique<Fiber>(PrivateTag {}, id, std::move(body), stack_size, std::move(resource));
}

Fiber::Fiber(PrivateTag, Id id, Body body, std::size_t stack_size, MemoryResourceSharedPtr resource)
    : BaseCoroutine(stack_size, std::move(resource))
    , id_(id)
    , body_(std::move(body)) {}

void Fiber::Continuation(CoroutineSuspendContext& ctx) {
    suspend_ctx_ = &ctx;
    body_();
    suspend_ctx_ = nullptr;
}

void Fiber::Run() {
    auto expected = FiberState::Ready;
    if (!state_.compare_exchange_strong(expected, FiberState::Running, std::memory_order_acq_rel)) {
        throw std::logic_error("Fiber::Run() called when fiber is not ready");
    }
    Resume();
}

void Fiber::Yield() {
    auto expected = FiberState::Running;
    if (!state_.compare_exchange_strong(expected, FiberState::Ready, std::memory_order_acq_rel)) {
        throw std::logic_error("Fiber::Yield() called when fiber is not running");
    }
    Suspend();
}

void Fiber::Complete() noexcept {
    suspend_ctx_ = nullptr;
    state_.store(FiberState::Finished, std::memory_order_release);
}

void Fiber::Suspend() {
    if (!suspend_ctx_) {
        throw std::logic_error("Cannot suspend fiber without a suspend context");
    }
    suspend_ctx_->Suspend();
}

} // namespace cortex::fiber::detail
