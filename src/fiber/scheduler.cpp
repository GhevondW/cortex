#include <cortex/config.hpp>
#include <cortex/fiber/scheduler.hpp>

#include <chrono>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>

namespace cortex::fiber {

namespace {
constexpr std::size_t kNoWorker = std::numeric_limits<std::size_t>::max();

thread_local Scheduler* g_current_scheduler = nullptr;
thread_local detail::Fiber* g_current_fiber = nullptr;
thread_local std::size_t g_current_worker = kNoWorker;
} // namespace

Scheduler::Scheduler(Config config)
    : config_(std::move(config)) {
    if (config_.default_stack_size == 0) {
        throw std::invalid_argument("Scheduler default_stack_size must be > 0");
    }
    if (!config_.memory_resource) {
        throw std::invalid_argument("Scheduler memory_resource must not be null");
    }

    if (config_.worker_threads == 0) {
        const auto hc = std::thread::hardware_concurrency();
        config_.worker_threads = hc == 0U ? 1U : static_cast<std::size_t>(hc);
    }

#ifdef CORTEX_EMSCRIPTEN
    use_inline_worker_ = true;
    worker_count_ = 1;
#else
    use_inline_worker_ = false;
    worker_count_ = config_.worker_threads;
#endif

    worker_queues_.reserve(worker_count_);
    for (std::size_t i = 0; i < worker_count_; ++i) {
        worker_queues_.push_back(std::make_unique<WorkerQueue>());
    }

    StartWorkers();
    running_.store(true, std::memory_order_release);
}

Scheduler::~Scheduler() {
    Stop();
    Wait();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
    running_.store(false, std::memory_order_release);

    std::lock_guard lock(fibers_mutex_);
    fibers_.clear();
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

void Scheduler::StartWorkers() {
    if (use_inline_worker_) {
        return;
    }

    workers_.reserve(worker_count_);
    for (std::size_t worker_index = 0; worker_index < worker_count_; ++worker_index) {
        workers_.emplace_back([this, worker_index] {
            WorkerLoop(worker_index);
        });
    }
}

void Scheduler::Stop() {
    const bool was_stopping = stopping_.exchange(true, std::memory_order_acq_rel);
    if (was_stopping) {
        return;
    }

    work_cv_.notify_all();
    done_cv_.notify_all();
}

void Scheduler::Wait() {
    if (use_inline_worker_) {
        RunInlineLoop();
        return;
    }

    std::unique_lock lock(done_mutex_);
    done_cv_.wait(lock, [this] {
        return active_fibers_.load(std::memory_order_acquire) == 0;
    });
}

void Scheduler::RunInlineLoop() {
    if (!use_inline_worker_) {
        return;
    }

    const auto prev_scheduler = g_current_scheduler;
    const auto prev_fiber = g_current_fiber;
    const auto prev_worker = g_current_worker;

    g_current_scheduler = this;
    g_current_worker = 0;

    while (active_fibers_.load(std::memory_order_acquire) > 0) {
        detail::Fiber* fiber = nullptr;
        if (!TryPopLocal(0, fiber)) {
            if (stopping_.load(std::memory_order_acquire) && ready_count_.load(std::memory_order_acquire) == 0) {
                break;
            }
            std::this_thread::yield();
            continue;
        }

        g_current_fiber = fiber;
        try {
            fiber->Run();
        } catch (...) {
            // Exceptions are captured by fiber::Future state wrapper when needed.
        }
        g_current_fiber = nullptr;

        if (fiber->IsDone()) {
            HandleFiberCompletion(fiber);
        } else if (fiber->IsReady()) {
            EnqueueReady(fiber, 0);
        }
    }

    g_current_scheduler = prev_scheduler;
    g_current_fiber = prev_fiber;
    g_current_worker = prev_worker;
}

void Scheduler::WorkerLoop(std::size_t worker_index) {
    g_current_scheduler = this;
    g_current_worker = worker_index;

    while (true) {
        detail::Fiber* fiber = nullptr;
        const bool has_local = TryPopLocal(worker_index, fiber);
        const bool has_stolen = has_local ? false : TrySteal(worker_index, fiber);

        if (!has_local && !has_stolen) {
            std::unique_lock lock(work_mutex_);
            work_cv_.wait_for(lock, std::chrono::milliseconds(1), [this] {
                return stopping_.load(std::memory_order_acquire) || ready_count_.load(std::memory_order_acquire) > 0;
            });

            if (stopping_.load(std::memory_order_acquire) && ready_count_.load(std::memory_order_acquire) == 0 &&
                active_fibers_.load(std::memory_order_acquire) == 0) {
                break;
            }
            continue;
        }

        g_current_fiber = fiber;
        try {
            fiber->Run();
        } catch (...) {
            // Exceptions are captured by fiber::Future state wrapper when needed.
        }
        g_current_fiber = nullptr;

        if (fiber->IsDone()) {
            HandleFiberCompletion(fiber);
        } else if (fiber->IsReady()) {
            EnqueueReady(fiber, worker_index);
        }
    }

    g_current_scheduler = nullptr;
    g_current_fiber = nullptr;
    g_current_worker = kNoWorker;
}

void Scheduler::HandleFiberCompletion(detail::Fiber* fiber) {
    if (!fiber) {
        return;
    }

    fiber->Complete();
    RemoveFiber(fiber->GetId());

    const auto remaining = active_fibers_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (remaining == 0) {
        done_cv_.notify_all();
        work_cv_.notify_all();
    }
}

void Scheduler::RemoveFiber(detail::Fiber::Id id) {
    std::lock_guard lock(fibers_mutex_);
    fibers_.erase(id);
}

detail::Fiber::Id Scheduler::SpawnFiberInternal(detail::Fiber::Body func, std::size_t stack_size) {
    auto fiber = detail::Fiber::Make(std::move(func), stack_size, config_.memory_resource);
    const auto fiber_id = fiber->GetId();
    auto* raw_fiber = fiber.get();

    {
        std::lock_guard lock(fibers_mutex_);
        fibers_.emplace(fiber_id, std::move(fiber));
    }

    active_fibers_.fetch_add(1, std::memory_order_acq_rel);
    EnqueueReady(raw_fiber, CurrentWorkerIndex());
    return fiber_id;
}

void Scheduler::EnqueueReady(detail::Fiber* fiber, std::optional<std::size_t> preferred_worker) {
    if (!fiber) {
        return;
    }

    const auto worker_index = PickWorker(preferred_worker);
    auto& queue = *worker_queues_[worker_index];

    {
        std::lock_guard lock(queue.mutex);
        queue.queue.push_back(fiber);
    }

    ready_count_.fetch_add(1, std::memory_order_release);
    work_cv_.notify_one();
}

bool Scheduler::TryPopLocal(std::size_t worker_index, detail::Fiber*& out_fiber) {
    auto& queue = *worker_queues_[worker_index];
    std::lock_guard lock(queue.mutex);
    if (queue.queue.empty()) {
        return false;
    }

    out_fiber = queue.queue.front();
    queue.queue.pop_front();
    ready_count_.fetch_sub(1, std::memory_order_acq_rel);
    return true;
}

bool Scheduler::TrySteal(std::size_t worker_index, detail::Fiber*& out_fiber) {
    if (!config_.enable_work_stealing || worker_count_ < 2) {
        return false;
    }

    for (std::size_t offset = 1; offset < worker_count_; ++offset) {
        const auto victim_index = (worker_index + offset) % worker_count_;
        auto& victim_queue = *worker_queues_[victim_index];
        std::unique_lock lock(victim_queue.mutex, std::try_to_lock);
        if (!lock.owns_lock() || victim_queue.queue.empty()) {
            continue;
        }

        out_fiber = victim_queue.queue.back();
        victim_queue.queue.pop_back();
        ready_count_.fetch_sub(1, std::memory_order_acq_rel);
        return true;
    }

    return false;
}

std::size_t Scheduler::PickWorker(std::optional<std::size_t> preferred_worker) noexcept {
    if (preferred_worker.has_value() && *preferred_worker < worker_count_) {
        return *preferred_worker;
    }

    const auto next = round_robin_worker_.fetch_add(1, std::memory_order_relaxed);
    return next % worker_count_;
}

std::optional<std::size_t> Scheduler::CurrentWorkerIndex() const noexcept {
    if (g_current_scheduler != this || g_current_worker == kNoWorker) {
        return std::nullopt;
    }
    return g_current_worker;
}

detail::Fiber* Scheduler::GetCurrentFiber() const noexcept {
    if (g_current_scheduler != this) {
        return nullptr;
    }
    return g_current_fiber;
}

void Scheduler::YieldCurrent() {
    auto* current = GetCurrentFiber();
    if (!current) {
        throw std::logic_error("No fiber is currently running");
    }

    current->Yield();
}

bool Scheduler::HasOtherReadyFibers() const noexcept {
    return ready_count_.load(std::memory_order_acquire) > 0;
}

bool Scheduler::IsDone() const noexcept {
    return active_fibers_.load(std::memory_order_acquire) == 0;
}

bool Scheduler::IsRunning() const noexcept {
    return running_.load(std::memory_order_acquire);
}

bool Scheduler::IsStopping() const noexcept {
    return stopping_.load(std::memory_order_acquire);
}

std::size_t Scheduler::GetWorkerCount() const noexcept {
    return worker_count_;
}

std::size_t Scheduler::GetDefaultStackSize() const noexcept {
    return config_.default_stack_size;
}

bool Scheduler::IsWorkStealingEnabled() const noexcept {
    return config_.enable_work_stealing;
}

MemoryResourceSharedPtr Scheduler::GetMemoryResource() const noexcept {
    return config_.memory_resource;
}

} // namespace cortex::fiber
