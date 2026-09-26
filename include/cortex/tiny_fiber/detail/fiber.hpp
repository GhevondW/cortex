#pragma once

#include <cortex/base_coroutine.hpp>
#include <cortex/coroutine_suspend_context.hpp>
#include <cortex/memory_resource.hpp>
#include <cortex/tiny_fiber/detail/wait_queue.hpp>

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include <function2/function2.hpp>

namespace cortex::tiny_fiber {
class Scheduler;
} // namespace cortex::tiny_fiber

namespace cortex::tiny_fiber::detail {

// Sleeping fibers, ordered by wake-up time. A fiber keeps the iterator of its
// entry so waking it early (Stop, cancellation) erases the entry at once and
// no stale timer is ever left behind.
using TimerMap = std::multimap<std::chrono::steady_clock::time_point, FiberId>;

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
    using Id = FiberId;
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

    [[nodiscard]] FiberState GetState() const noexcept {
        return state_;
    }

    // Start a new wait: advances the epoch and returns a token for it. Record
    // the token in the queue you are about to park on, then call Park().
    [[nodiscard]] WaiterRef PrepareWait() noexcept {
        return WaiterRef {id_, ++wait_epoch_};
    }

    [[nodiscard]] std::uint64_t GetWaitEpoch() const noexcept {
        return wait_epoch_;
    }

    // What the fiber is parked in (for diagnostics); nullptr when not parked.
    [[nodiscard]] const char* GetWaitReason() const noexcept {
        return wait_reason_;
    }

    // Optional human-readable name (diagnostics only).
    [[nodiscard]] const std::string& GetName() const noexcept {
        return name_;
    }

    void SetName(std::string_view name) {
        name_.assign(name);
    }

    // Timer entry of the current sleep, if any.
    void ArmTimer(TimerMap::iterator entry) noexcept {
        timer_ = entry;
        timer_armed_ = true;
    }

    [[nodiscard]] bool HasTimer() const noexcept {
        return timer_armed_;
    }

    TimerMap::iterator DisarmTimer() noexcept {
        timer_armed_ = false;
        return timer_;
    }

    // Whether the current park may be interrupted by cancellation.
    [[nodiscard]] bool IsCancellablePark() const noexcept {
        return cancellable_park_;
    }

    // Run this fiber (Ready -> Running, resumes coroutine execution)
    void Run();

    // Yield control back to the scheduler (Running -> Ready, suspends)
    void Yield();

    // Park until woken (Running -> Suspended, suspends). `reason` must be a
    // string literal; it is reported by Scheduler diagnostics.
    void Park(const char* reason, bool cancellable);

    // Wake a parked fiber (Suspended -> Ready)
    void Wake();

    // Mark fiber as finished.
    void Complete();

private:
    void Continuation(CoroutineSuspendContext& ctx) override;

    // Suspend the coroutine (yields control back to the resumer)
    void Suspend();

private:
    Id id_;
    FiberState state_ {FiberState::Ready};
    Body body_;
    CoroutineSuspendContext* suspend_ctx_ {nullptr};
    std::uint64_t wait_epoch_ {0};
    const char* wait_reason_ {nullptr};
    bool cancellable_park_ {false};
    bool timer_armed_ {false};
    TimerMap::iterator timer_ {};
    std::string name_;
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
