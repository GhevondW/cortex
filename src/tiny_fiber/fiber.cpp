#include <cortex/tiny_fiber/detail/fiber.hpp>

#include <cassert>
#include <stdexcept>
#include <utility>

namespace cortex::tiny_fiber::detail {

std::unique_ptr<Fiber> Fiber::Make(Body body, std::size_t stack_size, MemoryResourceSharedPtr resource) {
    static Id g_fiber_id = 1;
    return std::make_unique<Fiber>(PrivateTag {}, g_fiber_id++, std::move(body), stack_size, std::move(resource));
}

Fiber::Fiber(PrivateTag, Id id, Body body, std::size_t stack_size, MemoryResourceSharedPtr resource)
    : BaseCoroutine(stack_size, std::move(resource))
    , id_(id)
    , body_(std::move(body)) {}

void Fiber::Continuation(CoroutineSuspendContext& ctx) {
    // Store suspend context so Yield() can use it
    suspend_ctx_ = &ctx;

    // Execute the user's function
    body_();

    // Clear context when done
    suspend_ctx_ = nullptr;
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

std::vector<Fiber*> Fiber::TakeWaiters() {
    std::vector<Fiber*> waiters(std::move(waiters_));
    waiters_.clear();
    return waiters;
}

} // namespace cortex::tiny_fiber::detail
