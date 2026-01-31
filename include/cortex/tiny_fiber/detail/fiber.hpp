#pragma once

#include <cortex/coroutine.hpp>
#include <cortex/coroutine_suspend_context.hpp>
#include <cortex/memory_resource.hpp>

#include <cstdint>
#include <deque>
#include <exception>
#include <memory>

namespace cortex::tiny_fiber::detail {

class Scheduler;

/**
 * @brief Internal fiber states
 */
enum class FiberState : std::uint8_t {
    Ready, // In ready queue, waiting to run
    Running, // Currently executing
    Suspended, // Waiting for something (Future, Mutex, CondVar)
    Finished // Completed execution
};

/**
 * @brief Internal fiber representation
 *
 * Wraps a cortex::Coroutine and tracks state for the scheduler.
 */
class Fiber {
public:
    using Id = std::uint64_t;

    Fiber(Id id, Coroutine coroutine);

    Fiber(const Fiber&) = delete;
    Fiber& operator=(const Fiber&) = delete;
    Fiber(Fiber&&) = default;
    Fiber& operator=(Fiber&&) = default;

    [[nodiscard]] Id GetId() const noexcept {
        return id_;
    }
    [[nodiscard]] FiberState GetState() const noexcept {
        return state_;
    }
    [[nodiscard]] bool IsDone() const noexcept {
        return coroutine_.IsDone();
    }
    [[nodiscard]] bool HasException() const noexcept {
        return exception_ != nullptr;
    }
    [[nodiscard]] std::exception_ptr GetException() const noexcept {
        return exception_;
    }

    void SetState(FiberState state) noexcept {
        state_ = state;
    }
    void SetException(std::exception_ptr ex) noexcept {
        exception_ = std::move(ex);
    }

    void Resume();

    // Suspend context management - set when fiber starts running
    void SetSuspendContext(CoroutineSuspendContext* ctx) noexcept {
        suspend_ctx_ = ctx;
    }
    [[nodiscard]] CoroutineSuspendContext* GetSuspendContext() const noexcept {
        return suspend_ctx_;
    }

    // Suspend the fiber (must be called while fiber is running)
    void Suspend();

    // Fibers waiting for this fiber to complete
    void AddWaiter(Fiber* waiter);
    std::deque<Fiber*> TakeWaiters();

private:
    Id id_;
    FiberState state_ {FiberState::Ready};
    Coroutine coroutine_;
    CoroutineSuspendContext* suspend_ctx_ {nullptr};
    std::exception_ptr exception_ {nullptr};
    std::deque<Fiber*> waiters_;
};

} // namespace cortex::tiny_fiber::detail
