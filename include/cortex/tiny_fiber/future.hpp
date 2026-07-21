#pragma once

#include <cortex/detail/forced_unwind.hpp>
#include <cortex/tiny_fiber/detail/fiber.hpp>
#include <cortex/tiny_fiber/scheduler.hpp>

#include <cassert>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

/**
 * @file future.hpp
 * @brief Future types for awaiting fiber results.
 */

namespace cortex::tiny_fiber {

namespace detail {

// Shared state for a fiber's result (internal).
template <typename T>
struct FutureState {
    Fiber::Id fiber_id {0};
    std::optional<T> result;
    std::exception_ptr exception;
    bool retrieved {false};
};

template <>
struct FutureState<void> {
    Fiber::Id fiber_id {0};
    std::exception_ptr exception;
    bool retrieved {false};
};

} // namespace detail

/**
 * @class Future
 * @brief Handle to a spawned fiber that returns a value.
 *
 * @tparam T The return type of the fiber.
 */
template <typename T>
class Future {
public:
    Future(const Future&) = delete;
    Future& operator=(const Future&) = delete;

    Future(Future&& other) noexcept
        : state_(std::move(other.state_))
        , scheduler_(other.scheduler_)
        , sched_alive_(std::move(other.sched_alive_)) {
        other.scheduler_ = nullptr;
    }

    Future& operator=(Future&& other) noexcept {
        if (this != &other) {
            // WaitImpl can throw SchedulerStoppingError; swallow it here since
            // operator= is noexcept and we're abandoning the old state anyway.
            if (state_ && scheduler_ && !state_->retrieved) {
                try {
                    WaitImpl();
                } catch (...) {
                }
            }
            state_ = std::move(other.state_);
            scheduler_ = other.scheduler_;
            sched_alive_ = std::move(other.sched_alive_);
            other.scheduler_ = nullptr;
        }
        return *this;
    }

    ~Future() {
        if (state_ && scheduler_ && !state_->retrieved) {
            try {
                WaitImpl();
            } catch (...) {
                // Suppress exceptions in destructor.
            }
        }
    }

    /**
     * @brief Block current fiber until this fiber completes.
     */
    void Wait() {
        if (!state_) {
            throw std::logic_error("Future has no state");
        }
        WaitImpl();
    }

    /**
     * @brief Block until fiber completes and return the result.
     *
     * @return The result of the fiber.
     * @throws Any exception thrown by the fiber.
     */
    T Get() {
        if (!state_) {
            throw std::logic_error("Future has no state");
        }
        if (state_->retrieved) {
            throw std::logic_error("Future result already retrieved");
        }

        WaitImpl();
        state_->retrieved = true;

        if (state_->exception) {
            std::rethrow_exception(state_->exception);
        }

        if (!state_->result.has_value()) {
            throw std::logic_error("Fiber completed without result");
        }

        return std::move(*state_->result);
    }

    /**
     * @brief Check if the fiber has completed.
     */
    [[nodiscard]] bool IsReady() const noexcept {
        if (!state_ || !scheduler_ || sched_alive_.expired()) {
            return true;
        }
        auto* fiber = scheduler_->GetFiber(state_->fiber_id);
        return !fiber || fiber->IsDone();
    }

private:
    template <typename F>
    friend auto Spawn(F&& func) -> Future<std::invoke_result_t<F>>;

    template <typename F>
    friend auto Spawn(F&& func, std::size_t stack_size) -> Future<std::invoke_result_t<F>>;

    Future(std::shared_ptr<detail::FutureState<T>> state, Scheduler* scheduler, std::weak_ptr<void> alive)
        : state_(std::move(state))
        , scheduler_(scheduler)
        , sched_alive_(std::move(alive)) {}

    void WaitImpl() {
        assert(state_);
        // A Future may outlive its scheduler. If the scheduler is gone there is
        // nothing to wait on, and touching it would be a use-after-free.
        if (!scheduler_ || sched_alive_.expired()) {
            return;
        }

        auto* fiber = scheduler_->GetFiber(state_->fiber_id);
        if (!fiber || fiber->IsDone()) {
            return;
        }

        auto* current = scheduler_->GetCurrentFiber();
        if (current) {
            fiber->AddWaiter(current->GetId());
            scheduler_->SuspendCurrent();
        }
    }

    std::shared_ptr<detail::FutureState<T>> state_;
    Scheduler* scheduler_ {nullptr};
    std::weak_ptr<void> sched_alive_;
};

/**
 * @brief Specialization for void-returning fibers.
 */
template <>
class Future<void> {
public:
    Future(const Future&) = delete;
    Future& operator=(const Future&) = delete;

    Future(Future&& other) noexcept
        : state_(std::move(other.state_))
        , scheduler_(other.scheduler_)
        , sched_alive_(std::move(other.sched_alive_)) {
        other.scheduler_ = nullptr;
    }

    Future& operator=(Future&& other) noexcept {
        if (this != &other) {
            if (state_ && scheduler_ && !state_->retrieved) {
                try {
                    WaitImpl();
                } catch (...) {
                }
            }
            state_ = std::move(other.state_);
            scheduler_ = other.scheduler_;
            sched_alive_ = std::move(other.sched_alive_);
            other.scheduler_ = nullptr;
        }
        return *this;
    }

    ~Future() {
        if (state_ && scheduler_ && !state_->retrieved) {
            try {
                WaitImpl();
            } catch (...) {
            }
        }
    }

    /**
     * @brief Block current fiber until this fiber completes.
     *
     * @throws Any exception thrown by the fiber.
     */
    void Wait() {
        if (!state_) {
            throw std::logic_error("Future has no state");
        }
        WaitImpl();
        state_->retrieved = true;

        if (state_->exception) {
            std::rethrow_exception(state_->exception);
        }
    }

    /**
     * @brief Check if the fiber has completed.
     */
    [[nodiscard]] bool IsReady() const noexcept {
        if (!state_ || !scheduler_ || sched_alive_.expired()) {
            return true;
        }
        auto* fiber = scheduler_->GetFiber(state_->fiber_id);
        return !fiber || fiber->IsDone();
    }

private:
    template <typename F>
    friend auto Spawn(F&& func) -> Future<std::invoke_result_t<F>>;

    template <typename F>
    friend auto Spawn(F&& func, std::size_t stack_size) -> Future<std::invoke_result_t<F>>;

    Future(std::shared_ptr<detail::FutureState<void>> state, Scheduler* scheduler, std::weak_ptr<void> alive)
        : state_(std::move(state))
        , scheduler_(scheduler)
        , sched_alive_(std::move(alive)) {}

    void WaitImpl() {
        assert(state_);
        // A Future may outlive its scheduler. If the scheduler is gone there is
        // nothing to wait on, and touching it would be a use-after-free.
        if (!scheduler_ || sched_alive_.expired()) {
            return;
        }

        auto* fiber = scheduler_->GetFiber(state_->fiber_id);
        if (!fiber || fiber->IsDone()) {
            return;
        }

        auto* current = scheduler_->GetCurrentFiber();
        if (current) {
            fiber->AddWaiter(current->GetId());
            scheduler_->SuspendCurrent();
        }
    }

    std::shared_ptr<detail::FutureState<void>> state_;
    Scheduler* scheduler_ {nullptr};
    std::weak_ptr<void> sched_alive_;
};

/**
 * @brief Spawn a new fiber with custom stack size.
 *
 * The fiber's exception (if any) is captured in the shared state without
 * propagating through the scheduler — the awaiter retrieves it via Get/Wait.
 *
 * @param func The function to execute in the fiber.
 * @param stack_size The stack size for the fiber.
 * @return A Future to wait for the result.
 */
template <typename F>
auto Spawn(F&& func, std::size_t stack_size) -> Future<std::invoke_result_t<F>> {
    using ResultType = std::invoke_result_t<F>;
    using StateAllocator = MemoryResourceAllocator<detail::FutureState<ResultType>>;

    auto& scheduler = Scheduler::Current();
    // Allocate the shared state from the scheduler's (pooled) resource. The
    // allocator keeps the resource alive, so a Future may still legally
    // outlive its scheduler.
    auto state = std::allocate_shared<detail::FutureState<ResultType>>(StateAllocator(scheduler.GetMemoryResource()));

    if constexpr (std::is_void_v<ResultType>) {
        state->fiber_id = scheduler.SpawnFiberInternal(
            [state, f = std::forward<F>(func)]() mutable {
                try {
                    f();
                } catch (const cortex::detail::ForcedUnwind&) {
                    // Internal unwind sentinel — must reach the coroutine
                    // boundary, never be captured as the fiber's result.
                    throw;
                } catch (...) {
                    state->exception = std::current_exception();
                }
            },
            stack_size);
    } else {
        state->fiber_id = scheduler.SpawnFiberInternal(
            [state, f = std::forward<F>(func)]() mutable {
                try {
                    state->result.emplace(f());
                } catch (const cortex::detail::ForcedUnwind&) {
                    // Internal unwind sentinel — must reach the coroutine
                    // boundary, never be captured as the fiber's result.
                    throw;
                } catch (...) {
                    state->exception = std::current_exception();
                }
            },
            stack_size);
    }

    return Future<ResultType>(std::move(state), &scheduler, scheduler.AliveTokenInternal());
}

/**
 * @brief Spawn a new fiber.
 *
 * @param func The function to execute in the fiber.
 * @return A Future to wait for the result.
 */
template <typename F>
auto Spawn(F&& func) -> Future<std::invoke_result_t<F>> {
    return Spawn(std::forward<F>(func), Scheduler::Current().GetDefaultStackSize());
}

} // namespace cortex::tiny_fiber
