#pragma once

#include <cortex/base_coroutine.hpp>
#include <cortex/coroutine_suspend_context.hpp>
#include <cortex/memory_resource.hpp>

#include <cstdint>
#include <deque>
#include <exception>
#include <memory>

#include <function2/function2.hpp>

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
 * Inherits from BaseCoroutine to get proper coroutine lifecycle management.
 * The user's function is stored and called from Continuation().
 */
class Fiber final : public BaseCoroutine {
private:
    struct PrivateTag {};

public:
    using Id = std::uint64_t;
    using Body = fu2::unique_function<void()>;

    /**
     * @brief Create a new fiber.
     *
     * @param id Unique fiber ID
     * @param body The function to execute
     * @param stack_size Stack size in bytes
     * @param resource Memory resource for allocation
     * @return unique_ptr to the new Fiber
     */
    static std::unique_ptr<Fiber> Make(Body body, std::size_t stack_size, MemoryResourceSharedPtr resource);

    // Constructor is public but requires PrivateTag (only Make can call it)
    Fiber(PrivateTag, Id id, Body body, std::size_t stack_size, MemoryResourceSharedPtr resource);

    ~Fiber() override = default;

    Fiber(const Fiber&) = delete;
    Fiber& operator=(const Fiber&) = delete;
    Fiber(Fiber&&) = delete;
    Fiber& operator=(Fiber&&) = delete;

    [[nodiscard]] Id GetId() const noexcept {
        return id_;
    }

    [[nodiscard]] FiberState GetState() const noexcept {
        return state_;
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
    void Continuation(CoroutineSuspendContext& ctx) override;

private:
    Id id_;
    FiberState state_ {FiberState::Ready};
    Body body_;
    CoroutineSuspendContext* suspend_ctx_ {nullptr};
    std::exception_ptr exception_ {nullptr};
    std::deque<Fiber*> waiters_;
};

} // namespace cortex::tiny_fiber::detail
