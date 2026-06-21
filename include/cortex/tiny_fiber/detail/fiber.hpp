#pragma once

#include <cortex/base_coroutine.hpp>
#include <cortex/coroutine_suspend_context.hpp>
#include <cortex/memory_resource.hpp>

#include <cstdint>
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
    using Body = fu2::unique_function<void()>;

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

    // Mark fiber as finished and return the IDs of fibers waiting on it.
    // IDs (not pointers) so callers can validate liveness via Scheduler::GetFiber.
    std::vector<Id> Complete();

    // Add the ID of a fiber that is waiting for this fiber to finish.
    void AddWaiter(Id waiter_id);

private:
    void Continuation(CoroutineSuspendContext& ctx) override;

    // Suspend the coroutine (yields control back to the resumer)
    void Suspend();

private:
    Id id_;
    FiberState state_ {FiberState::Ready};
    Body body_;
    CoroutineSuspendContext* suspend_ctx_ {nullptr};
    std::vector<Id> waiters_;
};

} // namespace cortex::tiny_fiber::detail
