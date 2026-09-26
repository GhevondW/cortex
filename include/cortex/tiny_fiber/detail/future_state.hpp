#pragma once

// Internal header (included at the end of <cortex/tiny_fiber/scheduler.hpp>).
// Holds the shared state behind Future/Promise, the fiber bodies used by
// Spawn/SpawnDetached, and Scheduler's template members that depend on them.

#include <cortex/detail/forced_unwind.hpp>
#include <cortex/memory_resource.hpp>
#include <cortex/tiny_fiber/detail/wait_queue.hpp>
#include <cortex/tiny_fiber/errors/deadlock_error.hpp>
#include <cortex/tiny_fiber/scheduler.hpp>

#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace cortex::tiny_fiber::detail {

// Shared state behind a Future. Produced either by a spawned fiber (Spawn) or
// by a Promise. Single-threaded: only touched on its scheduler's thread.
struct FutureStateBase {
    Scheduler* scheduler {nullptr};
    std::weak_ptr<void> alive; // expires when `scheduler` is destroyed
    FiberId fiber_id {0}; // producing fiber; 0 when promise-backed
    bool ready {false};
    bool retrieved {false};
    bool detached {false}; // nobody will observe the result
    bool external {false}; // completed from outside fibers (Promise)
    std::exception_ptr exception;
    WaiterList waiters;

    [[nodiscard]] Scheduler* LiveScheduler() const noexcept {
        return alive.expired() ? nullptr : scheduler;
    }

    // Publish the result: marks ready and wakes every waiting fiber.
    void MarkReady();

    // Complete with an exception. A detached state reports it to the
    // scheduler as unhandled (unless it is a cancellation).
    void Fail(std::exception_ptr ex);

    // The producer was destroyed without completing: BrokenPromiseError.
    void Abandon();
};

template <typename T>
struct FutureState : FutureStateBase {
    std::optional<T> result;
};

template <>
struct FutureState<void> : FutureStateBase {};

template <typename T>
std::shared_ptr<FutureState<T>> MakeFutureState(Scheduler& scheduler, bool external) {
    // Allocate from the scheduler's (pooled) resource. The allocator keeps
    // the resource alive, so the state may outlive the scheduler.
    using Allocator = MemoryResourceAllocator<FutureState<T>>;
    auto state = std::allocate_shared<FutureState<T>>(Allocator(scheduler.GetMemoryResource()));
    state->scheduler = &scheduler;
    state->alive = scheduler.AliveTokenInternal();
    state->external = external;
    return state;
}

// Marks the calling fiber's park as a wait on the outside world (see
// Scheduler::Status::kWaiting) for as long as the scope lives.
class ExternalWaitScope {
public:
    ExternalWaitScope(Scheduler& scheduler, bool external) noexcept
        : scheduler_(external ? &scheduler : nullptr) {
        if (scheduler_ != nullptr) {
            scheduler_->BeginExternalWaitInternal();
        }
    }

    ~ExternalWaitScope() {
        if (scheduler_ != nullptr) {
            scheduler_->EndExternalWaitInternal();
        }
    }

    ExternalWaitScope(const ExternalWaitScope&) = delete;
    ExternalWaitScope& operator=(const ExternalWaitScope&) = delete;

private:
    Scheduler* scheduler_;
};

// Parks the calling fiber until `state` is ready. Throws std::logic_error if
// the caller is not a fiber of the state's scheduler. When `cancellable`, also
// throws SchedulerStoppingError once the scheduler is stopping and
// CancelledError if the calling fiber is cancelled; otherwise (a destructor's
// join) it waits regardless, cancelling the producing fiber if the calling
// fiber is or becomes cancelled.
void AwaitState(FutureStateBase& state, bool cancellable);

// Like AwaitState (cancellable), but gives up at `deadline`. Returns whether
// the state became ready.
bool AwaitStateUntil(FutureStateBase& state, Scheduler::TimePoint deadline);

// Parks until one of `states` is ready and returns its index. A null state
// counts as ready. All states must belong to the same scheduler.
std::size_t AwaitAnyState(FutureStateBase* const* states, std::size_t count);

// Moves the result out (or rethrows the stored exception) and marks the
// state retrieved.
template <typename T>
T TakeResult(FutureState<T>& state) {
    state.retrieved = true;
    if (state.exception) {
        std::rethrow_exception(state.exception);
    }
    if constexpr (!std::is_void_v<T>) {
        if (!state.result.has_value()) {
            throw std::logic_error("Fiber completed without result");
        }
        return std::move(*state.result);
    }
}

// Called first by every fiber body: a fiber whose scheduler is already
// stopping (or that was cancelled before it started) never runs user code.
void ThrowIfInterruptedAtStart();

// Report an exception nobody can observe (detached fiber). Cancellations are
// the normal way for a fiber to end and are ignored.
void ReportUnhandled(std::exception_ptr ex);

// Fiber body for Spawn: runs `func` and publishes its result into `state`.
template <typename R, typename F>
auto MakeSpawnBody(std::shared_ptr<FutureState<R>> state, F&& func) {
    return [state = std::move(state), f = std::forward<F>(func)]() mutable {
        try {
            ThrowIfInterruptedAtStart();
            if constexpr (std::is_void_v<R>) {
                f();
            } else {
                state->result.emplace(f());
            }
            state->MarkReady();
        } catch (const cortex::detail::ForcedUnwind&) {
            // Internal unwind sentinel: it must reach the coroutine boundary,
            // and the Future must never see it.
            state->Abandon();
            throw;
        } catch (...) {
            state->Fail(std::current_exception());
        }
    };
}

// Fiber body for fire-and-forget fibers (SpawnDetached, Scheduler::Create).
template <typename F>
auto MakeDetachedBody(F&& func) {
    return [f = std::forward<F>(func)]() mutable {
        try {
            ThrowIfInterruptedAtStart();
            static_cast<void>(f());
        } catch (const cortex::detail::ForcedUnwind&) {
            throw;
        } catch (...) {
            ReportUnhandled(std::current_exception());
        }
    };
}

} // namespace cortex::tiny_fiber::detail

namespace cortex::tiny_fiber {

// Scheduler template members
template <typename F>
auto Scheduler::Run(F&& entry) -> std::invoke_result_t<F> {
    return Run(std::forward<F>(entry), Config {});
}

template <typename F>
auto Scheduler::Run(F&& entry, Config config) -> std::invoke_result_t<F> {
    using ResultType = std::invoke_result_t<F>;

    std::shared_ptr<detail::FutureState<ResultType>> state;
    std::exception_ptr failure;
    bool entry_finished = false;
    {
        Scheduler scheduler(std::move(config));
        state = detail::MakeFutureState<ResultType>(scheduler, /*external=*/false);
        state->fiber_id = scheduler.SpawnFiberInternal(detail::MakeSpawnBody<ResultType>(state, std::forward<F>(entry)),
                                                       scheduler.config_.default_stack_size);
        try {
            if (scheduler.RunToCompletion() == Status::kDeadlocked) {
                failure = std::make_exception_ptr(DeadlockError(scheduler.DeadlockMessage()));
            }
        } catch (...) {
            failure = std::current_exception(); // unhandled exception from a detached fiber
        }
        entry_finished = state->ready;
    } // Tear the scheduler down with no exception in flight.

    // The entry's own exception is the most specific error; otherwise report
    // a detached fiber's failure or the deadlock.
    if (failure && !(entry_finished && state->exception)) {
        std::rethrow_exception(failure);
    }
    return detail::TakeResult(*state);
}

template <typename F>
std::unique_ptr<Scheduler> Scheduler::Create(F&& entry) {
    return Create(std::forward<F>(entry), Config {});
}

template <typename F>
std::unique_ptr<Scheduler> Scheduler::Create(F&& entry, Config config) {
    std::unique_ptr<Scheduler> scheduler(new Scheduler(std::move(config)));
    scheduler->SpawnFiberInternal(detail::MakeDetachedBody(std::forward<F>(entry)),
                                  scheduler->config_.default_stack_size);
    return scheduler;
}

} // namespace cortex::tiny_fiber
