#pragma once

#include <cortex/base_coroutine.hpp>
#include <cortex/coroutine_suspend_context.hpp>
#include <cortex/memory_resource.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <function2/function2.hpp>

namespace cortex::tiny_fiber {
class Scheduler;
} // namespace cortex::tiny_fiber

namespace cortex::tiny_fiber::detail {

// Internal fiber states
enum class FiberState : std::uint8_t {
    Ready, // In ready queue, waiting to run
    Running, // Currently executing
    Suspended, // Waiting for something (Future, Mutex, CondVar)
    Finished // Completed execution
};

// Internal fiber representation.
// Inherits from BaseCoroutine to get proper coroutine lifecycle management.
// The user's function is stored and called from Continuation().
class Fiber final : public BaseCoroutine {
public:
    using Id = std::uint64_t;
    // 64 bytes of inline storage: the Spawn() wrapper captures a shared_ptr
    // (16 bytes) plus the user functor, so the fu2 default of 16 bytes would
    // heap-allocate for every non-empty user lambda.
    using Body = fu2::function_base<true, false, fu2::capacity_fixed<64>, true, false, void()>;

    // Construction goes through Scheduler::SpawnFiberInternal so the scheduler can
    // assign unique IDs. The constructor takes the ID directly.
    Fiber(Id id, Body body, std::size_t stack_size, MemoryResourceSharedPtr resource);

    ~Fiber() override = default;

    Fiber(const Fiber&) = delete;
    Fiber& operator=(const Fiber&) = delete;
    Fiber(Fiber&&) = delete;
    Fiber& operator=(Fiber&&) = delete;

    [[nodiscard]] Id GetId() const noexcept {
        return id_;
    }

    [[nodiscard]] bool IsSuspended() const noexcept {
        return state_ == FiberState::Suspended;
    }

    // Run this fiber (Ready -> Running, resumes coroutine execution)
    void Run();

    // Yield control back to the scheduler (Running -> Ready, suspends)
    void Yield();

    // Park until woken by another fiber (Running -> Suspended, suspends)
    void Park();

    // Wake a parked fiber (Suspended -> Ready)
    void Wake();

    // Mark fiber as finished. The recorded waiters stay readable via
    // ForEachWaiter until the fiber is destroyed.
    void Complete();

    // Add the ID of a fiber that is waiting for this fiber to finish.
    void AddWaiter(Id waiter_id);

    // Visit the IDs of fibers waiting on this one. IDs (not pointers) so
    // callers can validate liveness via Scheduler::GetFiber.
    template <typename F>
    void ForEachWaiter(F&& func) const {
        for (std::uint8_t i = 0; i < inline_waiter_count_; ++i) {
            func(inline_waiters_[i]);
        }
        for (Id id : overflow_waiters_) {
            func(id);
        }
    }

private:
    void Continuation(CoroutineSuspendContext& ctx) override;

    // Suspend the coroutine (yields control back to the resumer)
    void Suspend();

private:
    Id id_;
    FiberState state_ {FiberState::Ready};
    Body body_;
    CoroutineSuspendContext* suspend_ctx_ {nullptr};
    // Waiter IDs. Almost always 0 or 1 (a Future's Get/Wait), so the first
    // few live inline to avoid a heap allocation per join.
    std::array<Id, 2> inline_waiters_ {};
    std::uint8_t inline_waiter_count_ {0};
    std::vector<Id> overflow_waiters_;
};

// Deleter for fibers placement-constructed in MemoryResource storage. Holds
// the resource raw: the Scheduler destroys its fibers before releasing its
// memory resource.
struct FiberDeleter {
    MemoryResource* resource;

    void operator()(Fiber* fiber) const {
        fiber->~Fiber();
        resource->Deallocate(fiber, sizeof(Fiber), alignof(Fiber));
    }
};

using FiberPtr = std::unique_ptr<Fiber, FiberDeleter>;

} // namespace cortex::tiny_fiber::detail
