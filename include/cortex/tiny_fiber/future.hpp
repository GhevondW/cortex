#pragma once

#include <cortex/tiny_fiber/detail/future_state.hpp>
#include <cortex/tiny_fiber/errors/broken_promise_error.hpp>
#include <cortex/tiny_fiber/errors/cancelled_error.hpp>
#include <cortex/tiny_fiber/scheduler.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

/**
 * @file future.hpp
 * @brief Future types for awaiting fiber results.
 */

namespace cortex::tiny_fiber {

template <typename T>
class Future;

template <typename T>
class Promise;

namespace detail {

// Members shared by Future<T> and Future<void>.
template <typename T>
class FutureBase {
public:
    FutureBase(const FutureBase&) = delete;
    FutureBase& operator=(const FutureBase&) = delete;

    FutureBase(FutureBase&& other) noexcept = default;

    FutureBase& operator=(FutureBase&& other) noexcept {
        if (this != &other) {
            JoinOnDestroy();
            state_ = std::move(other.state_);
        }
        return *this;
    }

    ~FutureBase() {
        JoinOnDestroy();
    }

    /**
     * @brief Check whether the result is available (Get()/Wait() won't block).
     *
     * Also true for a detached or moved-from Future.
     */
    [[nodiscard]] bool IsReady() const noexcept {
        return !state_ || state_->ready;
    }

    /**
     * @brief Block the current fiber until the result is ready or `timeout`
     *        elapsed.
     *
     * @return true if the result is ready.
     * @throws SchedulerStoppingError / CancelledError like Wait().
     */
    template <typename Rep, typename Period>
    bool WaitFor(std::chrono::duration<Rep, Period> timeout) {
        auto& state = RequireState();
        if (state.ready) {
            return true;
        }
        Scheduler* scheduler = state.LiveScheduler();
        if (scheduler == nullptr) {
            return AwaitStateUntil(state, Scheduler::TimePoint::max()); // throws the clear error
        }
        const auto now = scheduler->Now();
        const auto step = std::chrono::ceil<Scheduler::Duration>(timeout);
        const auto deadline = step >= Scheduler::TimePoint::max() - now ? Scheduler::TimePoint::max() : now + step;
        return AwaitStateUntil(state, deadline);
    }

    /**
     * @brief Block the current fiber until the result is ready or the
     *        scheduler's clock reaches `deadline`.
     *
     * @return true if the result is ready.
     */
    bool WaitUntil(Scheduler::TimePoint deadline) {
        return AwaitStateUntil(RequireState(), deadline);
    }

    /**
     * @brief Ask the fiber to stop.
     *
     * The fiber throws CancelledError at its next cancellation point (Yield,
     * CheckPoint, SleepFor/Until, Future::Wait/Get, ConditionVariable::Wait,
     * Channel operations), and is woken if it is parked in one. A fiber that
     * has not started never runs. Get()/Wait() then rethrow CancelledError
     * unless the fiber caught it. No effect once the fiber finished.
     */
    void Cancel() noexcept {
        if (!state_ || state_->ready || state_->fiber_id == 0) {
            return;
        }
        if (Scheduler* scheduler = state_->LiveScheduler()) {
            scheduler->CancelFiber(state_->fiber_id);
        }
    }

    /**
     * @brief Stop tracking the fiber: the destructor no longer waits for it.
     *
     * The fiber keeps running. If it later fails, its exception is reported
     * to the scheduler as unhandled. After Detach() the Future is empty.
     */
    void Detach() noexcept {
        if (state_) {
            state_->detached = true;
            state_.reset();
        }
    }

    /// @cond INTERNAL
    [[nodiscard]] FutureStateBase* StateInternal() const noexcept {
        return state_.get();
    }
    /// @endcond

protected:
    explicit FutureBase(std::shared_ptr<FutureState<T>> state) noexcept
        : state_(std::move(state)) {}

    FutureState<T>& RequireState() const {
        if (!state_) {
            throw std::logic_error("Future has no state");
        }
        return *state_;
    }

    std::shared_ptr<FutureState<T>> state_;

private:
    // A Future of a spawned fiber joins it on destruction, when that is
    // possible: from a fiber of the same, non-stopping scheduler. Elsewhere
    // (plain code, a destroyed scheduler) the fiber simply keeps running.
    // A cancelled parent cancels the child before joining it, so cancellation
    // reaches the whole tree of fibers it spawned.
    void JoinOnDestroy() noexcept {
        if (!state_ || state_->ready || state_->fiber_id == 0) {
            return;
        }
        Scheduler* scheduler = state_->LiveScheduler();
        if (scheduler == nullptr || Scheduler::TryCurrent() != scheduler || scheduler->GetCurrentFiber() == nullptr ||
            scheduler->IsStopping()) {
            return;
        }
        if (scheduler->GetCurrentFiber()->IsCancelRequested()) {
            scheduler->CancelFiber(state_->fiber_id);
        }
        try {
            AwaitState(*state_, /*cancellable=*/false);
        } catch (...) {
            // Stopping while joining: abandon the wait.
        }
    }
};

} // namespace detail

/**
 * @class Future
 * @brief Handle to the result of a spawned fiber.
 *
 * Move-only. Destroying a Future whose fiber is still running waits for the
 * fiber (when called from a fiber of the same scheduler), so children never
 * outlive the scope that spawned them. Use Detach() or SpawnDetached() for
 * fire-and-forget work.
 *
 * Get() and Wait() may be called from any fiber of the owning scheduler; once
 * the result is ready they may also be called from plain code. Calling them
 * from plain code before the result is ready throws std::logic_error.
 *
 * @tparam T The return type of the fiber.
 */
template <typename T>
class Future : public detail::FutureBase<T> {
public:
    /**
     * @brief Block the current fiber until the result is ready.
     *
     * Does not rethrow the fiber's exception (Get() does).
     *
     * @throws SchedulerStoppingError if the scheduler is stopping.
     * @throws std::logic_error if not ready and not called from a fiber of
     *         the owning scheduler.
     */
    void Wait() {
        detail::AwaitState(this->RequireState(), /*cancellable=*/true);
    }

    /**
     * @brief Block the current fiber until the result is ready and return it.
     *
     * Can be called once: the value is moved out.
     *
     * @return The result of the fiber.
     * @throws Any exception thrown by the fiber (CancelledError if it was
     *         cancelled or its scheduler stopped, BrokenPromiseError if it
     *         was destroyed before finishing).
     */
    T Get() {
        auto& state = this->RequireState();
        if (state.retrieved) {
            throw std::logic_error("Future result already retrieved");
        }
        detail::AwaitState(state, /*cancellable=*/true);
        return detail::TakeResult(state);
    }

private:
    template <typename F>
    friend auto Spawn(F&& func, std::size_t stack_size) -> Future<std::invoke_result_t<F>>;
    template <typename>
    friend class Promise;

    explicit Future(std::shared_ptr<detail::FutureState<T>> state) noexcept
        : detail::FutureBase<T>(std::move(state)) {}
};

/**
 * @brief Specialization for void-returning fibers.
 */
template <>
class Future<void> : public detail::FutureBase<void> {
public:
    /**
     * @brief Block the current fiber until the fiber finished.
     *
     * May be called repeatedly.
     *
     * @throws Any exception thrown by the fiber.
     */
    void Wait() {
        auto& state = RequireState();
        detail::AwaitState(state, /*cancellable=*/true);
        state.retrieved = true;
        if (state.exception) {
            std::rethrow_exception(state.exception);
        }
    }

    /**
     * @brief Same as Wait(): block until finished, rethrow its exception.
     */
    void Get() {
        Wait();
    }

private:
    template <typename F>
    friend auto Spawn(F&& func, std::size_t stack_size) -> Future<std::invoke_result_t<F>>;
    template <typename>
    friend class Promise;

    explicit Future(std::shared_ptr<detail::FutureState<void>> state) noexcept
        : detail::FutureBase<void>(std::move(state)) {}
};

/**
 * @brief Spawn a new fiber with a custom stack size.
 *
 * The fiber's exception (if any) is captured in its Future without
 * propagating through the scheduler — the awaiter retrieves it via Get/Wait.
 *
 * @param func The function to execute in the fiber.
 * @param stack_size The stack size for the fiber.
 * @return A Future for the result. Dropping it joins the fiber immediately.
 */
template <typename F>
[[nodiscard("dropping the Future waits for the fiber right away; use SpawnDetached for fire-and-forget")]]
auto Spawn(F&& func, std::size_t stack_size) -> Future<std::invoke_result_t<F>> {
    using ResultType = std::invoke_result_t<F>;

    auto& scheduler = Scheduler::Current();
    auto state = detail::MakeFutureState<ResultType>(scheduler, /*external=*/false);
    state->fiber_id =
        scheduler.SpawnFiberInternal(detail::MakeSpawnBody<ResultType>(state, std::forward<F>(func)), stack_size);
    return Future<ResultType>(std::move(state));
}

/**
 * @brief Spawn a new fiber.
 *
 * @param func The function to execute in the fiber.
 * @return A Future for the result. Dropping it joins the fiber immediately.
 */
template <typename F>
[[nodiscard("dropping the Future waits for the fiber right away; use SpawnDetached for fire-and-forget")]]
auto Spawn(F&& func) -> Future<std::invoke_result_t<F>> {
    return Spawn(std::forward<F>(func), Scheduler::Current().GetDefaultStackSize());
}

/**
 * @brief Spawn a fire-and-forget fiber with a custom stack size.
 *
 * Nobody waits for it. An exception escaping it (other than a cancellation)
 * is reported to the scheduler as unhandled.
 */
template <typename F>
void SpawnDetached(F&& func, std::size_t stack_size) {
    Scheduler::Current().SpawnFiberInternal(detail::MakeDetachedBody(std::forward<F>(func)), stack_size);
}

/**
 * @brief Spawn a fire-and-forget fiber.
 */
template <typename F>
void SpawnDetached(F&& func) {
    SpawnDetached(std::forward<F>(func), Scheduler::Current().GetDefaultStackSize());
}

} // namespace cortex::tiny_fiber
