#pragma once

#include <cortex/tiny_fiber/detail/fiber.hpp>
#include <cortex/tiny_fiber/scheduler.hpp>

#include <cassert>
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

/**
 * @brief Shared state for a fiber's result.
 */
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
        , scheduler_(other.scheduler_) {
        other.scheduler_ = nullptr;
    }

    Future& operator=(Future&& other) noexcept {
        if (this != &other) {
            // Wait for current fiber if needed
            if (state_ && scheduler_ && !state_->retrieved) {
                WaitImpl();
            }
            state_ = std::move(other.state_);
            scheduler_ = other.scheduler_;
            other.scheduler_ = nullptr;
        }
        return *this;
    }

    ~Future() {
        // Destructor waits for fiber completion
        if (state_ && scheduler_ && !state_->retrieved) {
            try {
                WaitImpl();
            } catch (...) {
                // Suppress exceptions in destructor
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

        // Check for exception (stored in state for safety after fiber cleanup)
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
        if (!state_ || !scheduler_) {
            return true;
        }
        auto* fiber = scheduler_->GetFiber(state_->fiber_id);
        return !fiber || fiber->IsDone();
    }

private:
    template <typename U>
    friend Future<U> Spawn(std::function<U()> func);

    template <typename U>
    friend Future<U> Spawn(std::function<U()> func, std::size_t stack_size);

    template <typename F>
    friend auto Spawn(F&& func) -> Future<std::invoke_result_t<F>>;

    template <typename F>
    friend auto Spawn(F&& func, std::size_t stack_size) -> Future<std::invoke_result_t<F>>;

    Future(std::shared_ptr<detail::FutureState<T>> state, Scheduler* scheduler)
        : state_(std::move(state))
        , scheduler_(scheduler) {}

    void WaitImpl() {
        assert(state_);
        assert(scheduler_);

        auto* fiber = scheduler_->GetFiber(state_->fiber_id);
        if (!fiber || fiber->IsDone()) {
            // Fiber already done or cleaned up, exception already in state
            return;
        }

        // Add current fiber as waiter
        auto* current = scheduler_->GetCurrentFiber();
        if (current) {
            fiber->AddWaiter(current);
            scheduler_->SuspendCurrent();
        }
        // Exception is stored in state by Spawn wrapper, no need to copy here
    }

    std::shared_ptr<detail::FutureState<T>> state_;
    Scheduler* scheduler_ {nullptr};
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
        , scheduler_(other.scheduler_) {
        other.scheduler_ = nullptr;
    }

    Future& operator=(Future&& other) noexcept {
        if (this != &other) {
            if (state_ && scheduler_ && !state_->retrieved) {
                WaitImpl();
            }
            state_ = std::move(other.state_);
            scheduler_ = other.scheduler_;
            other.scheduler_ = nullptr;
        }
        return *this;
    }

    ~Future() {
        if (state_ && scheduler_ && !state_->retrieved) {
            try {
                WaitImpl();
            } catch (...) {
                // Suppress exceptions in destructor
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

        // Check for exception (stored in state for safety after fiber cleanup)
        if (state_->exception) {
            std::rethrow_exception(state_->exception);
        }
    }

    /**
     * @brief Check if the fiber has completed.
     */
    [[nodiscard]] bool IsReady() const noexcept {
        if (!state_ || !scheduler_) {
            return true;
        }
        auto* fiber = scheduler_->GetFiber(state_->fiber_id);
        return !fiber || fiber->IsDone();
    }

private:
    template <typename U>
    friend Future<U> Spawn(std::function<U()> func);

    template <typename U>
    friend Future<U> Spawn(std::function<U()> func, std::size_t stack_size);

    template <typename F>
    friend auto Spawn(F&& func) -> Future<std::invoke_result_t<F>>;

    template <typename F>
    friend auto Spawn(F&& func, std::size_t stack_size) -> Future<std::invoke_result_t<F>>;

    Future(std::shared_ptr<detail::FutureState<void>> state, Scheduler* scheduler)
        : state_(std::move(state))
        , scheduler_(scheduler) {}

    void WaitImpl() {
        assert(state_);
        assert(scheduler_);

        auto* fiber = scheduler_->GetFiber(state_->fiber_id);
        if (!fiber || fiber->IsDone()) {
            // Fiber already done or cleaned up, exception already in state
            return;
        }

        auto* current = scheduler_->GetCurrentFiber();
        if (current) {
            fiber->AddWaiter(current);
            scheduler_->SuspendCurrent();
        }
        // Exception is stored in state by Spawn wrapper, no need to copy here
    }

    std::shared_ptr<detail::FutureState<void>> state_;
    Scheduler* scheduler_ {nullptr};
};

/**
 * @brief Spawn a new fiber with custom stack size.
 *
 * @param func The function to execute in the fiber.
 * @param stack_size The stack size for the fiber.
 * @return A Future to wait for the result.
 */
template <typename F>
auto Spawn(F&& func, std::size_t stack_size) -> Future<std::invoke_result_t<F>> {
    using ResultType = std::invoke_result_t<F>;

    auto& scheduler = Scheduler::Current();
    auto state = std::make_shared<detail::FutureState<ResultType>>();

    if constexpr (std::is_void_v<ResultType>) {
        auto fiber_id = scheduler.SpawnFiberInternal(
            [state, f = std::forward<F>(func)]() mutable {
                try {
                    f();
                } catch (...) {
                    // Store exception in state immediately (fiber may be cleaned up later)
                    state->exception = std::current_exception();
                    throw; // Re-throw so scheduler knows fiber failed
                }
            },
            stack_size);
        state->fiber_id = fiber_id;
    } else {
        auto fiber_id = scheduler.SpawnFiberInternal(
            [state, f = std::forward<F>(func)]() mutable {
                try {
                    state->result = f();
                } catch (...) {
                    // Store exception in state immediately (fiber may be cleaned up later)
                    state->exception = std::current_exception();
                    throw;
                }
            },
            stack_size);
        state->fiber_id = fiber_id;
    }

    return Future<ResultType>(std::move(state), &scheduler);
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
