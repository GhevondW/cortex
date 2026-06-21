#include <cortex/tiny_fiber/scheduler.hpp>

#include <cassert>
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
    : config_(std::move(config)) {}

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
    // Clearing fibers_ triggers forced unwinding for any that didn't exit.
    fibers_.clear();
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
        // Complete() returns waiter IDs; resolve each via the fiber map and
        // skip any that are gone or already runnable.
        auto waiter_ids = current_fiber_->Complete();
        for (auto id : waiter_ids) {
            auto* waiter = GetFiber(id);
            if (waiter && waiter->IsSuspended()) {
                Schedule(waiter);
            }
        }

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
    const auto id = next_fiber_id_++;
    auto fiber = std::make_unique<detail::Fiber>(id, std::move(func), stack_size, config_.memory_resource);
    auto* fiber_raw_ptr = fiber.get();

    fibers_.emplace(id, std::move(fiber));
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

    ready_queue_.push_back(current_fiber_);
    current_fiber_->Yield();
}

bool Scheduler::HasOtherReadyFibers() const {
    return !ready_queue_.empty();
}

} // namespace cortex::tiny_fiber
