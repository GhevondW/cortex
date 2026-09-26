#include <cortex/guarded_stack_resource.hpp>
#include <cortex/tiny_fiber/errors/scheduler_stopping_error.hpp>
#include <cortex/tiny_fiber/scheduler.hpp>

#include <algorithm>
#include <cassert>
#include <new>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace cortex::tiny_fiber {

namespace {
// Thread-local current scheduler (works in single-threaded WASM too)
thread_local Scheduler* g_current_scheduler = nullptr;

// Makes `scheduler` the current one for a scope and restores the previous
// value afterwards, so a scheduler driven from inside another scheduler's
// fiber hands "current" back to the outer one when it stops running.
class CurrentSchedulerScope {
public:
    explicit CurrentSchedulerScope(Scheduler* scheduler) noexcept
        : previous_(std::exchange(g_current_scheduler, scheduler)) {}
    ~CurrentSchedulerScope() {
        g_current_scheduler = previous_;
    }

    CurrentSchedulerScope(const CurrentSchedulerScope&) = delete;
    CurrentSchedulerScope& operator=(const CurrentSchedulerScope&) = delete;

private:
    Scheduler* previous_;
};
} // namespace

MemoryResourceSharedPtr MakeDefaultFiberResource() {
    // Pool over guard-paged stacks where the platform supports it: recycled
    // stacks keep their guard page, so overflow faults without making every
    // Spawn pay for mmap.
    if (HasGuardPageSupport()) {
        return MakePooledMemoryResource(PooledMemoryResource::Config {.upstream = MakeGuardedStackResource()});
    }
    return MakePooledMemoryResource();
}

Scheduler& Scheduler::Current() {
    if (!g_current_scheduler) {
        throw std::logic_error("No scheduler is running. Scheduler::Current() must be called from within a fiber.");
    }
    return *g_current_scheduler;
}

Scheduler* Scheduler::TryCurrent() noexcept {
    return g_current_scheduler;
}

Scheduler::Scheduler(Config config)
    : config_(std::move(config)) {
    fiber_slots_.reserve(16);
    vacant_slots_.reserve(16);
}

Scheduler::~Scheduler() {
    if (!stopping_) {
        Stop();
    }

    // Be the current scheduler during final cleanup so fibers can resolve
    // Scheduler::Current() while unwinding.
    CurrentSchedulerScope scope(this);

    // Run remaining fibers so they catch SchedulerStoppingError and exit.
    while (!ready_queue_.empty()) {
        current_fiber_ = ready_queue_.front();
        ready_queue_.pop_front();

        if (current_fiber_ && !current_fiber_->IsDone()) {
            try {
                current_fiber_->Run();
            } catch (...) {
                // Ignore exceptions during shutdown.
            }
        }
        current_fiber_ = nullptr;
    }

    running_ = false;
    // Destroying the slots force-unwinds any fiber that did not exit. Move
    // them out first so that wake-ups triggered while they unwind (e.g. a
    // Future state being abandoned) find no fibers to wake.
    auto slots = std::move(fiber_slots_);
    fiber_slots_.clear();
    slots.clear();
}

void Scheduler::Stop() {
    if (stopping_) {
        return;
    }

    stopping_ = true;

    // Wake every suspended fiber so it can observe IsStopping() and exit. Each
    // fiber may still be referenced by stale entries in someone's waiter queue;
    // unlock/notify code paths skip stale entries, and Step() validates waiter
    // IDs on Complete(), so this is safe.
    for (auto& slot : fiber_slots_) {
        if (slot.fiber && slot.fiber->IsSuspended()) {
            WakeFiber(slot.fiber.get());
        }
    }
}

void Scheduler::ProcessPendingCleanup() {
    for (auto id : pending_cleanup_) {
        const auto index = static_cast<std::size_t>(id & kSlotIndexMask);
        if (index < fiber_slots_.size() && fiber_slots_[index].id == id) {
            fiber_slots_[index].fiber.reset();
            fiber_slots_[index].id = 0;
            vacant_slots_.push_back(static_cast<std::uint32_t>(index));
        }
    }
    pending_cleanup_.clear();
}

bool Scheduler::Step() {
    ProcessPendingCleanup();
    if (!timers_.empty()) {
        FireDueTimers(config_.clock());
    }

    if (ready_queue_.empty()) {
        running_ = false;
        return false;
    }

    {
        CurrentSchedulerScope scope(this);
        running_ = true;

        current_fiber_ = ready_queue_.front();
        ready_queue_.pop_front();
        ++step_count_;

        assert(current_fiber_);

        try {
            current_fiber_->Run();
        } catch (...) {
            // Fiber bodies catch everything themselves; anything reaching
            // here escaped the coroutine machinery. Nobody can observe it.
            ReportUnhandledInternal(std::current_exception());
        }

        if (current_fiber_->IsDone()) {
            current_fiber_->Complete();
            --live_fibers_;
            pending_cleanup_.push_back(current_fiber_->GetId());
        }

        current_fiber_ = nullptr;
    }

    const bool has_more = !ready_queue_.empty() || (!timers_.empty() && HasDueTimer(config_.clock()));
    if (!has_more) {
        running_ = false;
    }

    DeliverUnhandled();
    return has_more;
}

Scheduler::Status Scheduler::RunFor(Duration budget) {
    const TimePoint now = config_.clock();
    const TimePoint deadline = budget >= TimePoint::max() - now ? TimePoint::max() : now + budget;
    return RunUntil(deadline);
}

Scheduler::Status Scheduler::RunUntil(TimePoint deadline) {
    struct DeadlineScope {
        TimePoint& slot;
        TimePoint saved;
        ~DeadlineScope() {
            slot = saved;
        }
    } scope {run_deadline_, std::exchange(run_deadline_, deadline)};

    while (Step() && config_.clock() < deadline) {
    }
    return GetStatus();
}

std::optional<Scheduler::TimePoint> Scheduler::NextTimerDeadline() const {
    if (timers_.empty()) {
        return std::nullopt;
    }
    return timers_.begin()->first;
}

bool Scheduler::HasDueTimer(TimePoint now) const {
    return !timers_.empty() && timers_.begin()->first <= now;
}

void Scheduler::FireDueTimers(TimePoint now) {
    while (HasDueTimer(now)) {
        auto entry = timers_.begin();
        detail::Fiber* fiber = GetFiber(entry->second);
        timers_.erase(entry);
        if (fiber != nullptr && fiber->HasTimer()) {
            fiber->DisarmTimer();
            WakeFiber(fiber);
        }
    }
}

void Scheduler::SleepUntilInternal(TimePoint deadline) {
    ThrowIfInterrupted(true);
    if (current_fiber_ == nullptr) {
        throw std::logic_error("SleepFor()/SleepUntil() must be called from within a fiber");
    }
    if (deadline <= config_.clock()) {
        YieldCurrent();
        return;
    }
    (void)current_fiber_->PrepareWait(); // new epoch: tokens from earlier waits can't wake this one
    ParkCurrentUntil("SleepFor", /*cancellable=*/true, deadline);
    ThrowIfInterrupted(true);
}

void Scheduler::CheckPointCurrent() {
    // Upper bound on calls between two clock reads.
    constexpr std::uint32_t kMaxCheckpointStride = 4096;

    ThrowIfInterrupted(true);

    const bool new_slice = checkpoint_slice_ != step_count_;
    if (!new_slice && --checkpoint_countdown_ > 0) {
        return;
    }

    const TimePoint now = config_.clock();
    if (new_slice) {
        checkpoint_slice_ = step_count_;
        const bool slice_overflows = config_.time_slice >= TimePoint::max() - now;
        const TimePoint slice_end = slice_overflows ? TimePoint::max() : now + config_.time_slice;
        slice_deadline_ = std::min(slice_end, run_deadline_);
    } else {
        // Aim for ~16 clock reads per slice.
        const Duration since = now - last_checkpoint_;
        const Duration target = config_.time_slice / 16;
        if (since < target / 2 && checkpoint_stride_ < kMaxCheckpointStride) {
            checkpoint_stride_ *= 2;
        } else if (since > target * 2 && checkpoint_stride_ > 1) {
            checkpoint_stride_ /= 2;
        }
    }
    last_checkpoint_ = now;
    checkpoint_countdown_ = checkpoint_stride_;

    if (now >= slice_deadline_) {
        YieldCurrent();
    }
}

void Scheduler::WaitForWork() {
    if (auto deadline = NextTimerDeadline()) {
        const Duration delay = *deadline - config_.clock();
        if (delay > Duration::zero()) {
            std::this_thread::sleep_for(delay);
        }
    }
}

Scheduler::Status Scheduler::GetStatus() const {
    if (live_fibers_ == 0) {
        return Status::kDone;
    }
    if (!ready_queue_.empty() || current_fiber_ != nullptr) {
        return Status::kRunnable;
    }
    if (!timers_.empty()) {
        return HasDueTimer(config_.clock()) ? Status::kRunnable : Status::kWaiting;
    }
    return Status::kDeadlocked;
}

Scheduler::Status Scheduler::RunToCompletion() {
    for (;;) {
        while (Step()) {
        }
        const Status status = GetStatus();
        if (status == Status::kWaiting) {
            WaitForWork();
            continue;
        }
        if (status != Status::kRunnable) {
            ProcessPendingCleanup();
            return status;
        }
    }
}

std::string Scheduler::DescribeFibers() const {
    std::ostringstream out;
    out << live_fibers_ << " live fiber(s):";
    for (const auto& slot : fiber_slots_) {
        const detail::Fiber* fiber = slot.fiber.get();
        if (fiber == nullptr || fiber->IsDone()) {
            continue;
        }
        out << "\n  #" << (slot.id >> kSlotIndexBits);
        if (!fiber->GetName().empty()) {
            out << " \"" << fiber->GetName() << '"';
        }
        switch (fiber->GetState()) {
        case detail::FiberState::Ready:
            out << " ready";
            break;
        case detail::FiberState::Running:
            out << " running";
            break;
        case detail::FiberState::Suspended:
            out << " suspended";
            if (fiber->GetWaitReason() != nullptr) {
                out << " in " << fiber->GetWaitReason();
            }
            break;
        case detail::FiberState::Finished:
            out << " finished";
            break;
        }
    }
    return out.str();
}

std::string Scheduler::DeadlockMessage() const {
    return "tiny_fiber: deadlock - every remaining fiber is suspended and nothing can wake it.\n" +
           DescribeFibers();
}

void Scheduler::ReportUnhandledInternal(std::exception_ptr ex) {
    unhandled_.push_back(std::move(ex));
}

void Scheduler::DeliverUnhandled() {
    if (unhandled_.empty()) {
        return;
    }
    if (config_.on_unhandled_exception) {
        auto pending = std::move(unhandled_);
        unhandled_.clear();
        for (auto& ex : pending) {
            config_.on_unhandled_exception(ex);
        }
        return;
    }
    std::exception_ptr first = unhandled_.front();
    unhandled_.erase(unhandled_.begin());
    std::rethrow_exception(first);
}

detail::Fiber::Id Scheduler::SpawnFiberInternal(detail::Fiber::Body func, std::size_t stack_size) {
    std::uint32_t index = 0;
    if (!vacant_slots_.empty()) {
        index = vacant_slots_.back();
        vacant_slots_.pop_back();
    } else {
        if (fiber_slots_.size() > kSlotIndexMask) {
            throw std::length_error("Too many live fibers in one scheduler");
        }
        index = static_cast<std::uint32_t>(fiber_slots_.size());
        fiber_slots_.emplace_back();
    }

    const auto id = (next_sequence_++ << kSlotIndexBits) | index;

    // Place the fiber object itself in MemoryResource storage so that with
    // the default pooled resource a spawn performs no system allocations.
    const auto& resource = config_.memory_resource;
    void* memory = resource->Allocate(sizeof(detail::Fiber), alignof(detail::Fiber));

    detail::Fiber* fiber_raw_ptr = nullptr;
    try {
        fiber_raw_ptr = new (memory) detail::Fiber(id, std::move(func), stack_size, resource);
    } catch (...) {
        resource->Deallocate(memory, sizeof(detail::Fiber), alignof(detail::Fiber));
        vacant_slots_.push_back(index);
        throw;
    }

    fiber_slots_[index].id = id;
    fiber_slots_[index].fiber = detail::FiberPtr(fiber_raw_ptr, detail::FiberDeleter {resource.get()});
    ready_queue_.push_back(fiber_raw_ptr);
    ++live_fibers_;

    return id;
}

detail::Fiber* Scheduler::GetFiber(detail::Fiber::Id id) {
    const auto index = static_cast<std::size_t>(id & kSlotIndexMask);
    if (index < fiber_slots_.size() && fiber_slots_[index].id == id) {
        return fiber_slots_[index].fiber.get();
    }
    return nullptr;
}

const detail::Fiber* Scheduler::GetFiber(detail::Fiber::Id id) const {
    const auto index = static_cast<std::size_t>(id & kSlotIndexMask);
    if (index < fiber_slots_.size() && fiber_slots_[index].id == id) {
        return fiber_slots_[index].fiber.get();
    }
    return nullptr;
}

detail::WaiterRef Scheduler::PrepareWait() {
    if (!current_fiber_) {
        throw std::logic_error("No fiber is currently running");
    }
    return current_fiber_->PrepareWait();
}

void Scheduler::ParkCurrent(const char* reason, bool cancellable) {
    if (!current_fiber_) {
        throw std::logic_error("No fiber is currently running");
    }
    current_fiber_->Park(reason, cancellable);
}

void Scheduler::ParkCurrentUntil(const char* reason, bool cancellable, TimePoint deadline) {
    if (!current_fiber_) {
        throw std::logic_error("No fiber is currently running");
    }
    if (deadline != TimePoint::max()) {
        current_fiber_->ArmTimer(timers_.emplace(deadline, current_fiber_->GetId()));
    }
    current_fiber_->Park(reason, cancellable);
}

bool Scheduler::IsWaiting(detail::WaiterRef ref) const {
    const detail::Fiber* fiber = GetFiber(ref.id);
    return fiber != nullptr && fiber->IsSuspended() && fiber->GetWaitEpoch() == ref.epoch;
}

bool Scheduler::WakeIfWaiting(detail::WaiterRef ref) {
    if (!IsWaiting(ref)) {
        return false;
    }
    WakeFiber(GetFiber(ref.id));
    return true;
}

void Scheduler::ThrowIfInterrupted(bool cancellable) const {
    if (stopping_) {
        throw SchedulerStoppingError();
    }
    if (cancellable && current_fiber_ != nullptr && current_fiber_->IsCancelRequested()) {
        throw CancelledError();
    }
}

void Scheduler::CancelFiber(detail::FiberId id) {
    detail::Fiber* fiber = GetFiber(id);
    if (fiber == nullptr || fiber->IsDone()) {
        return;
    }
    fiber->RequestCancel();
    if (fiber->IsSuspended() && fiber->IsCancellablePark()) {
        WakeFiber(fiber);
    }
}

void Scheduler::WakeFiber(detail::Fiber* fiber) {
    assert(fiber && fiber->IsSuspended());
    if (fiber->HasTimer()) {
        timers_.erase(fiber->DisarmTimer()); // woken early: drop its timer
    }
    fiber->Wake();
    ready_queue_.push_back(fiber);
}

void Scheduler::YieldCurrent() {
    if (!current_fiber_) {
        throw std::logic_error("No fiber is currently running");
    }

    ready_queue_.push_back(current_fiber_);
    current_fiber_->Yield();
}

bool Scheduler::HasOtherReadyFibers() const {
    return !ready_queue_.empty();
}

} // namespace cortex::tiny_fiber
