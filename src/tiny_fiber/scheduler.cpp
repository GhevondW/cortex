#include <cortex/tiny_fiber/scheduler.hpp>

#include <cassert>
#include <new>
#include <stdexcept>
#include <utility>

namespace cortex::tiny_fiber {

namespace {
// Thread-local current scheduler (works in single-threaded WASM too)
thread_local Scheduler* g_current_scheduler = nullptr;
} // namespace

Scheduler& Scheduler::Current() {
    if (!g_current_scheduler) {
        throw std::logic_error("No scheduler is running. Scheduler::Current() must be called from within a fiber.");
    }
    return *g_current_scheduler;
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

    // Set ourselves as current scheduler during final cleanup so fibers can
    // resolve Scheduler::Current() while unwinding.
    g_current_scheduler = this;

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
    // Clearing the slots triggers forced unwinding for any that didn't exit.
    fiber_slots_.clear();
    g_current_scheduler = nullptr;
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
            Schedule(slot.fiber.get());
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
        if (running_) {
            running_ = false;
            g_current_scheduler = nullptr;
        }
        return false;
    }

    running_ = true;
    g_current_scheduler = this;

    current_fiber_ = ready_queue_.front();
    ready_queue_.pop_front();

    assert(current_fiber_);

    try {
        current_fiber_->Run();
    } catch (...) {
        // The entry fiber (created via Run() rather than Spawn()) has no
        // future to deliver the exception to. Swallow during cooperative
        // execution rather than tearing down the scheduler.
    }

    if (current_fiber_->IsDone()) {
        // Resolve each recorded waiter via the fiber map and skip any that
        // are gone or already runnable.
        current_fiber_->Complete();
        current_fiber_->ForEachWaiter([this](detail::Fiber::Id id) {
            auto* waiter = GetFiber(id);
            if (waiter && waiter->IsSuspended()) {
                Schedule(waiter);
            }
        });

        pending_cleanup_.push_back(current_fiber_->GetId());
    }

    current_fiber_ = nullptr;

    bool has_more = !ready_queue_.empty();
    if (!has_more) {
        running_ = false;
        g_current_scheduler = nullptr;
    }
    return has_more;
}

void Scheduler::RunLoop() {
    assert(!running_);
    assert(g_current_scheduler == nullptr);

    while (Step()) {
    }

    ProcessPendingCleanup();
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

    return id;
}

detail::Fiber* Scheduler::GetFiber(detail::Fiber::Id id) {
    const auto index = static_cast<std::size_t>(id & kSlotIndexMask);
    if (index < fiber_slots_.size() && fiber_slots_[index].id == id) {
        return fiber_slots_[index].fiber.get();
    }
    return nullptr;
}

void Scheduler::Schedule(detail::Fiber* fiber) {
    if (fiber) {
        fiber->Wake();
        ready_queue_.push_back(fiber);
    }
}

void Scheduler::SuspendCurrent() {
    if (!current_fiber_) {
        throw std::logic_error("No fiber is currently running");
    }

    current_fiber_->Park();
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
