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

void AwaitState(FutureStateBase& state, bool cancellable) {
    if (state.ready) {
        return;
    }

    Scheduler* scheduler = state.LiveScheduler();
    if (scheduler == nullptr) {
        throw std::logic_error("Future: the result is not ready and its scheduler has been destroyed");
    }
    if (Scheduler::TryCurrent() != scheduler || scheduler->GetCurrentFiber() == nullptr) {
        throw std::logic_error(
            "Future: the result is not ready and the caller is not a fiber of the owning scheduler. "
            "Call Get()/Wait() from a fiber, or drive the scheduler until IsReady().");
    }

    while (!state.ready) {
        scheduler->ThrowIfInterrupted(cancellable);
        state.waiters.Push(scheduler->PrepareWait());
        scheduler->ParkCurrent("Future::Wait", cancellable);
    }
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
