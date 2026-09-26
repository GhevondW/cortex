#include <cortex/tiny_fiber/errors/broken_promise_error.hpp>
#include <cortex/tiny_fiber/errors/cancelled_error.hpp>
#include <cortex/tiny_fiber/scheduler.hpp>

#include <stdexcept>

namespace cortex::tiny_fiber::detail {

namespace {

bool IsCancellation(const std::exception_ptr& ex) {
    try {
        std::rethrow_exception(ex);
    } catch (const CancelledError&) {
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

void FutureStateBase::MarkReady() {
    ready = true;
    if (Scheduler* live = LiveScheduler()) {
        waiters.WakeAll(*live);
    } else {
        waiters.Clear();
    }
}

void FutureStateBase::Fail(std::exception_ptr ex) {
    if (detached) {
        ReportUnhandled(ex);
    }
    exception = std::move(ex);
    MarkReady();
}

void FutureStateBase::Abandon() {
    if (ready) {
        return;
    }
    exception = std::make_exception_ptr(BrokenPromiseError());
    MarkReady();
}

namespace {

// Waits that give up early (timeouts, WaitAny) leave their token in the
// list; prune once it holds this many so it cannot grow without bound.
constexpr std::size_t kPruneThreshold = 8;

// The scheduler that `state`'s waiter must belong to, validated.
Scheduler& RequireWaitingFiberScheduler(const FutureStateBase& state) {
    Scheduler* scheduler = state.LiveScheduler();
    if (scheduler == nullptr) {
        throw std::logic_error("Future: the result is not ready and its scheduler has been destroyed");
    }
    if (Scheduler::TryCurrent() != scheduler || scheduler->GetCurrentFiber() == nullptr) {
        throw std::logic_error(
            "Future: the result is not ready and the caller is not a fiber of the owning scheduler. "
            "Call Get()/Wait() from a fiber, or drive the scheduler until IsReady().");
    }
    return *scheduler;
}

void Register(FutureStateBase& state, Scheduler& scheduler, WaiterRef ref) {
    if (state.waiters.Size() >= kPruneThreshold) {
        state.waiters.PruneStale(scheduler);
    }
    state.waiters.Push(ref);
}

} // namespace

void AwaitState(FutureStateBase& state, bool cancellable) {
    if (state.ready) {
        return;
    }
    Scheduler& scheduler = RequireWaitingFiberScheduler(state);
    while (!state.ready) {
        scheduler.ThrowIfInterrupted(cancellable);
        Register(state, scheduler, scheduler.PrepareWait());
        ExternalWaitScope external(scheduler, state.external);
        scheduler.ParkCurrent("Future::Wait", cancellable);
    }
}

bool AwaitStateUntil(FutureStateBase& state, Scheduler::TimePoint deadline) {
    if (state.ready) {
        return true;
    }
    Scheduler& scheduler = RequireWaitingFiberScheduler(state);
    while (!state.ready) {
        scheduler.ThrowIfInterrupted(true);
        if (scheduler.Now() >= deadline) {
            return false;
        }
        Register(state, scheduler, scheduler.PrepareWait());
        ExternalWaitScope external(scheduler, state.external);
        scheduler.ParkCurrentUntil("Future::WaitFor", true, deadline);
    }
    return true;
}

std::size_t AwaitAnyState(FutureStateBase* const* states, std::size_t count) {
    if (count == 0) {
        throw std::invalid_argument("WaitAny() needs at least one future");
    }
    const auto first_ready = [&]() -> std::size_t {
        for (std::size_t i = 0; i < count; ++i) {
            if (states[i] == nullptr || states[i]->ready) {
                return i;
            }
        }
        return count;
    };

    std::size_t ready = first_ready();
    if (ready != count) {
        return ready;
    }

    Scheduler& scheduler = RequireWaitingFiberScheduler(*states[0]);
    for (std::size_t i = 1; i < count; ++i) {
        if (states[i]->scheduler != &scheduler) {
            throw std::logic_error("WaitAny(): all futures must belong to the same scheduler");
        }
    }

    while (ready == count) {
        scheduler.ThrowIfInterrupted(true);
        // One token for this wait, registered everywhere: the first state to
        // become ready wakes the fiber, the rest become stale.
        const WaiterRef ref = scheduler.PrepareWait();
        bool any_external = false;
        for (std::size_t i = 0; i < count; ++i) {
            Register(*states[i], scheduler, ref);
            any_external = any_external || states[i]->external;
        }
        ExternalWaitScope external(scheduler, any_external);
        scheduler.ParkCurrent("WaitAny", true);
        ready = first_ready();
    }
    return ready;
}

void ThrowIfInterruptedAtStart() {
    Scheduler::Current().ThrowIfInterrupted(true);
}

void ReportUnhandled(std::exception_ptr ex) {
    if (IsCancellation(ex)) {
        return;
    }
    if (Scheduler* scheduler = Scheduler::TryCurrent()) {
        scheduler->ReportUnhandledInternal(std::move(ex));
    }
}

} // namespace cortex::tiny_fiber::detail
