#include <cortex/tiny_fiber/detail/wait_queue.hpp>
#include <cortex/tiny_fiber/scheduler.hpp>

namespace cortex::tiny_fiber::detail {

bool WaitQueue::WakeOne(Scheduler& scheduler) {
    while (!waiters_.empty()) {
        const WaiterRef ref = waiters_.front();
        waiters_.pop_front();
        if (scheduler.WakeIfWaiting(ref)) {
            return true;
        }
    }
    return false;
}

std::size_t WaitQueue::WakeAll(Scheduler& scheduler) {
    std::size_t woken = 0;
    while (!waiters_.empty()) {
        const WaiterRef ref = waiters_.front();
        waiters_.pop_front();
        if (scheduler.WakeIfWaiting(ref)) {
            ++woken;
        }
    }
    return woken;
}

void WaiterList::WakeAll(Scheduler& scheduler) {
    for (std::uint8_t i = 0; i < inline_count_; ++i) {
        scheduler.WakeIfWaiting(inline_[i]);
    }
    for (const WaiterRef& ref : overflow_) {
        scheduler.WakeIfWaiting(ref);
    }
    Clear();
}

} // namespace cortex::tiny_fiber::detail
