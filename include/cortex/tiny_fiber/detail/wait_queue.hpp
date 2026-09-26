#pragma once

#include <algorithm>
#include <array>
#include <concepts>
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

// Waits that end early — a timeout, WaitAny, a cancellation — leave their
// token behind. Containers prune such stale tokens, but only once they have
// doubled in size since the last prune, so pruning stays amortized O(1) per
// push even with many live waiters.
inline constexpr std::size_t kMinPruneSize = 16;

// FIFO of waiters, for primitives that hand wake-ups out one at a time
// (Mutex, ConditionVariable, Channel).
class WaitQueue {
public:
    // Adds `ref`; `is_live(token)` tells whether a queued token still names a
    // waiting fiber (normally Scheduler::IsWaiting).
    template <std::predicate<WaiterRef> IsLive>
    void Push(WaiterRef ref, IsLive&& is_live) {
        if (waiters_.size() >= prune_at_) {
            std::erase_if(waiters_, [&is_live](const WaiterRef& token) {
                return !is_live(token);
            });
            prune_at_ = std::max(kMinPruneSize, waiters_.size() * 2);
        }
        waiters_.push_back(ref);
    }

    // Push, pruning with Scheduler::IsWaiting.
    void Push(WaiterRef ref, const Scheduler& scheduler);

    // Wakes the first waiter whose token is still valid, discarding stale
    // ones on the way. Returns false if nobody was woken.
    bool WakeOne(Scheduler& scheduler);

    // Wakes every waiter whose token is still valid. Returns how many woke.
    std::size_t WakeAll(Scheduler& scheduler);

    [[nodiscard]] bool Empty() const noexcept {
        return waiters_.empty();
    }

    [[nodiscard]] std::size_t Size() const noexcept {
        return waiters_.size();
    }

private:
    std::deque<WaiterRef> waiters_;
    std::size_t prune_at_ {kMinPruneSize};
};

// Unordered set of waiters that are all woken together (a fiber's joiners, a
// future's waiters). Almost always 0 or 1 entries, so the first few live
// inline and a join performs no heap allocation.
class WaiterList {
public:
    // Adds `ref`; `is_live(token)` tells whether a listed token still names a
    // waiting fiber (normally Scheduler::IsWaiting).
    template <std::predicate<WaiterRef> IsLive>
    void Push(WaiterRef ref, IsLive&& is_live) {
        if (Size() >= prune_at_) {
            PruneStale(is_live);
            prune_at_ = std::max(kMinPruneSize, Size() * 2);
        }
        PushUnchecked(ref);
    }

    // Wakes every waiter whose token is still valid and clears the list.
    void WakeAll(Scheduler& scheduler);

    void Clear() noexcept {
        inline_count_ = 0;
        overflow_.clear();
        prune_at_ = kMinPruneSize;
    }

    [[nodiscard]] std::size_t Size() const noexcept {
        return inline_count_ + overflow_.size();
    }

private:
    void PushUnchecked(WaiterRef ref) {
        if (inline_count_ < inline_.size()) {
            inline_[inline_count_] = ref;
            ++inline_count_;
        } else {
            overflow_.push_back(ref);
        }
    }

    // Drops tokens whose wait already ended, keeping the order of the rest.
    template <typename IsLive>
    void PruneStale(IsLive& is_live) {
        std::vector<WaiterRef> all;
        all.reserve(Size());
        all.insert(all.end(), inline_.begin(), inline_.begin() + inline_count_);
        all.insert(all.end(), overflow_.begin(), overflow_.end());
        inline_count_ = 0;
        overflow_.clear();
        for (const WaiterRef& token : all) {
            if (is_live(token)) {
                PushUnchecked(token);
            }
        }
    }

    std::array<WaiterRef, 2> inline_ {};
    std::uint8_t inline_count_ {0};
    std::vector<WaiterRef> overflow_;
    std::size_t prune_at_ {kMinPruneSize};
};

} // namespace cortex::tiny_fiber::detail
