#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace cortex::tiny_fiber {
class Scheduler;
} // namespace cortex::tiny_fiber

namespace cortex::tiny_fiber::detail {

using FiberId = std::uint64_t;

// Identifies one specific suspension of one fiber. A fiber's epoch advances
// every time it prepares to wait, so a token recorded for an earlier wait can
// never wake a later, unrelated one — even if the fiber is suspended again by
// the time the stale token is popped. Waking goes through
// Scheduler::WakeIfWaiting, which checks id, state and epoch.
struct WaiterRef {
    FiberId id {0};
    std::uint64_t epoch {0};
};

// FIFO of waiters, for primitives that hand wake-ups out one at a time
// (Mutex, ConditionVariable, Channel).
class WaitQueue {
public:
    void Push(WaiterRef ref) {
        waiters_.push_back(ref);
    }

    // Wakes the first waiter whose token is still valid, discarding stale
    // ones on the way. Returns false if nobody was woken.
    bool WakeOne(Scheduler& scheduler);

    // Wakes every waiter whose token is still valid. Returns how many woke.
    std::size_t WakeAll(Scheduler& scheduler);

    [[nodiscard]] bool Empty() const noexcept {
        return waiters_.empty();
    }

private:
    std::deque<WaiterRef> waiters_;
};

// Unordered set of waiters that are all woken together (a fiber's joiners, a
// future's waiters). Almost always 0 or 1 entries, so the first few live
// inline and a join performs no heap allocation.
class WaiterList {
public:
    void Push(WaiterRef ref) {
        if (inline_count_ < inline_.size()) {
            inline_[inline_count_] = ref;
            ++inline_count_;
        } else {
            overflow_.push_back(ref);
        }
    }

    // Wakes every waiter whose token is still valid and clears the list.
    void WakeAll(Scheduler& scheduler);

    // Drops entries whose wait already ended. Waits that give up early (a
    // timeout, WaitAny) leave their token behind; pruning keeps a long-lived
    // list from growing without bound.
    void PruneStale(const Scheduler& scheduler);

    void Clear() noexcept {
        inline_count_ = 0;
        overflow_.clear();
    }

    [[nodiscard]] std::size_t Size() const noexcept {
        return inline_count_ + overflow_.size();
    }

private:
    std::array<WaiterRef, 2> inline_ {};
    std::uint8_t inline_count_ {0};
    std::vector<WaiterRef> overflow_;
};

} // namespace cortex::tiny_fiber::detail
