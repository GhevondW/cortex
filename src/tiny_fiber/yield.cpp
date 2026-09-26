#include <cortex/tiny_fiber/errors/scheduler_stopping_error.hpp>
#include <cortex/tiny_fiber/scheduler.hpp>
#include <cortex/tiny_fiber/yield.hpp>

#include <stdexcept>

namespace cortex::tiny_fiber {

void Yield() {
    auto& scheduler = Scheduler::Current();
    if (scheduler.IsStopping()) {
        throw SchedulerStoppingError();
    }
    scheduler.YieldCurrent();
}

bool YieldIfOthersReady() {
    auto& scheduler = Scheduler::Current();
    if (scheduler.IsStopping()) {
        throw SchedulerStoppingError();
    }
    if (scheduler.HasOtherReadyFibers()) {
        scheduler.YieldCurrent();
        return true;
    }
    return false;
}

void CheckPoint() {
    Scheduler* scheduler = Scheduler::TryCurrent();
    if (scheduler == nullptr || scheduler->GetCurrentFiber() == nullptr) {
        return; // plain code: nothing to yield to
    }
    scheduler->CheckPointCurrent();
}

bool IsStopping() {
    return Scheduler::Current().IsStopping();
}

namespace detail {

void SleepForImpl(std::chrono::steady_clock::duration duration) {
    Scheduler* scheduler = Scheduler::TryCurrent();
    if (scheduler == nullptr) {
        throw std::logic_error("SleepFor() must be called from within a fiber");
    }
    const auto now = scheduler->Now();
    const auto deadline =
        duration >= Scheduler::TimePoint::max() - now ? Scheduler::TimePoint::max() : now + duration;
    scheduler->SleepUntilInternal(deadline);
}

void SleepUntilImpl(std::chrono::steady_clock::time_point deadline) {
    Scheduler* scheduler = Scheduler::TryCurrent();
    if (scheduler == nullptr) {
        throw std::logic_error("SleepUntil() must be called from within a fiber");
    }
    scheduler->SleepUntilInternal(deadline);
}

} // namespace detail

void SetFiberName(std::string_view name) {
    auto* fiber = Scheduler::Current().GetCurrentFiber();
    if (fiber == nullptr) {
        throw std::logic_error("SetFiberName() must be called from within a fiber");
    }
    fiber->SetName(name);
}

} // namespace cortex::tiny_fiber
