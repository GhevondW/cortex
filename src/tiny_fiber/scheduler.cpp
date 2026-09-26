#include <cortex/guarded_stack_resource.hpp>
#include <cortex/tiny_fiber/errors/scheduler_stopping_error.hpp>
#include <cortex/tiny_fiber/scheduler.hpp>

#include <cassert>
#include <new>
#include <sstream>
#include <stdexcept>
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

    if (ready_queue_.empty()) {
        running_ = false;
        return false;
    }

    {
        CurrentSchedulerScope scope(this);
        running_ = true;

        current_fiber_ = ready_queue_.front();
        ready_queue_.pop_front();

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

    const bool has_more = !ready_queue_.empty();
    if (!has_more) {
        running_ = false;
    }

    DeliverUnhandled();
    return has_more;
}

Scheduler::Status Scheduler::GetStatus() const {
    if (live_fibers_ == 0) {
        return Status::kDone;
    }
    if (!ready_queue_.empty() || current_fiber_ != nullptr) {
        return Status::kRunnable;
    }
    return Status::kDeadlocked;
}

Scheduler::Status Scheduler::RunToCompletion() {
    for (;;) {
        while (Step()) {
        }
        const Status status = GetStatus();
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

bool Scheduler::WakeIfWaiting(detail::WaiterRef ref) {
    auto* fiber = GetFiber(ref.id);
    if (!fiber || !fiber->IsSuspended() || fiber->GetWaitEpoch() != ref.epoch) {
        return false;
    }
    WakeFiber(fiber);
    return true;
}

void Scheduler::ThrowIfInterrupted([[maybe_unused]] bool cancellable) const {
    if (stopping_) {
        throw SchedulerStoppingError();
    }
}

void Scheduler::WakeFiber(detail::Fiber* fiber) {
    assert(fiber && fiber->IsSuspended());
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
