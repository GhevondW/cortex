#include <cortex/fiber/errors/scheduler_stopping_error.hpp>
#include <cortex/fiber/scheduler.hpp>
#include <cortex/fiber/yield.hpp>

namespace cortex::fiber {

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
    if (!scheduler.HasOtherReadyFibers()) {
        return false;
    }

    scheduler.YieldCurrent();
    return true;
}

bool IsStopping() {
    return Scheduler::Current().IsStopping();
}

} // namespace cortex::fiber
