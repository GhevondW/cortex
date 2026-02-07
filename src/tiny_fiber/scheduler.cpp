#include <cortex/tiny_fiber/scheduler.hpp>

#include <cassert>
#include <stdexcept>

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
    : config_(std::move(config)) {}

Scheduler::~Scheduler() {
    // Signal stop and let fibers exit gracefully
    if (!stopping_) {
        Stop();
    }

    // Set ourselves as current scheduler during final cleanup
    g_current_scheduler = this;

    // Run remaining fibers to let them handle the stopping signal
    // They should catch SchedulerStoppingError and exit
    while (!ready_queue_.empty()) {
        current_fiber_ = ready_queue_.front();
        ready_queue_.pop_front();

        if (current_fiber_ && !current_fiber_->IsDone()) {
            try {
                current_fiber_->Run();
            } catch (...) {
                // Ignore exceptions during shutdown
            }
        }
        current_fiber_ = nullptr;
    }

    // Clear remaining state
    running_ = false;

    // Explicitly clear fibers (triggers forced unwinding for any that didn't exit)
    fibers_.clear();

    // Now clear the global scheduler pointer
    g_current_scheduler = nullptr;
}

void Scheduler::Stop() {
    if (stopping_) {
        return; // Already stopping
    }

    stopping_ = true;

    // Wake up all suspended fibers so they can exit gracefully
    for (auto& [id, fiber] : fibers_) {
        if (fiber && fiber->IsSuspended()) {
            Schedule(fiber.get());
        }
    }
}

void Scheduler::ProcessPendingCleanup() {
    for (auto id : pending_cleanup_) {
        fibers_.erase(id);
    }
    pending_cleanup_.clear();
}

bool Scheduler::Step() {
    // Clean up finished fibers from previous iteration
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
        current_fiber_->SetException(std::current_exception());
    }

    if (current_fiber_->IsDone()) {
        auto waiters = current_fiber_->Complete();
        for (auto* waiter : waiters) {
            if (waiter && waiter->IsSuspended()) {
                Schedule(waiter);
            }
        }

        // Schedule fiber for cleanup (will be deleted at start of next iteration)
        pending_cleanup_.push_back(current_fiber_->GetId());
    }

    current_fiber_ = nullptr;

    // Return true if there's more work
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

    // Clean up the last completed fiber(s)
    ProcessPendingCleanup();
}

detail::Fiber::Id Scheduler::SpawnFiberInternal(detail::Fiber::Body func, std::size_t stack_size) {
    auto fiber = detail::Fiber::Make(std::move(func), stack_size, config_.memory_resource);
    const auto id = fiber->GetId();
    auto* fiber_raw_ptr = fiber.get();

    fibers_[id] = std::move(fiber);

    // New fibers start in Ready state, just enqueue
    ready_queue_.push_back(fiber_raw_ptr);

    return id;
}

detail::Fiber* Scheduler::GetFiber(detail::Fiber::Id id) {
    auto it = fibers_.find(id);
    if (it != fibers_.end()) {
        return it->second.get();
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

    // Enqueue first, then yield (Yield sets Ready + suspends)
    ready_queue_.push_back(current_fiber_);
    current_fiber_->Yield();
}

bool Scheduler::HasOtherReadyFibers() const {
    return !ready_queue_.empty();
}

} // namespace cortex::tiny_fiber
