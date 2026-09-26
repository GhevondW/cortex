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

void WaiterList::PruneStale(const Scheduler& scheduler) {
    std::vector<WaiterRef> live;
    for (std::uint8_t i = 0; i < inline_count_; ++i) {
        if (scheduler.IsWaiting(inline_[i])) {
            live.push_back(inline_[i]);
        }
    }
    for (const WaiterRef& ref : overflow_) {
        if (scheduler.IsWaiting(ref)) {
            live.push_back(ref);
        }
    }
    Clear();
    for (const WaiterRef& ref : live) {
        Push(ref);
    }
}

} // namespace cortex::tiny_fiber::detail
