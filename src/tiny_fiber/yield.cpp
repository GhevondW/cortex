#include <cortex/tiny_fiber/scheduler.hpp>
#include <cortex/tiny_fiber/yield.hpp>

namespace cortex::tiny_fiber {

void Yield() {
    Scheduler::Current().YieldCurrent();
}

bool YieldIfOthersReady() {
    auto& scheduler = Scheduler::Current();
    if (scheduler.HasOtherReadyFibers()) {
        scheduler.YieldCurrent();
        return true;
    }
    return false;
}

} // namespace cortex::tiny_fiber
