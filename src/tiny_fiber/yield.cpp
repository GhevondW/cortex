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

bool IsStopping() {
    return Scheduler::Current().IsStopping();
}

void SetFiberName(std::string_view name) {
    auto* fiber = Scheduler::Current().GetCurrentFiber();
    if (fiber == nullptr) {
        throw std::logic_error("SetFiberName() must be called from within a fiber");
    }
    fiber->SetName(name);
}

} // namespace cortex::tiny_fiber
