#pragma once

#include <cortex/fiber/detail/fiber.hpp>
#include <cortex/fiber/detail/platform.hpp>
#include <cortex/memory_resource.hpp>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace cortex::fiber {

template <typename T>
class Future;

class Mutex;
class ConditionVariable;

template <typename F>
auto Spawn(F&& func) -> Future<std::invoke_result_t<F>>;

template <typename F>
auto Spawn(F&& func, std::size_t stack_size) -> Future<std::invoke_result_t<F>>;

void Yield();
bool YieldIfOthersReady();

class Scheduler {
public:
    struct Config {
        std::size_t default_stack_size = cortex::Coroutine::kDefaultStackSizeBytes;
        MemoryResourceSharedPtr memory_resource = GetDefaultMemoryResource();
        std::size_t worker_threads = 0;
        bool enable_work_stealing = true;
    };

    template <typename F>
    static void Run(F&& entry);

    template <typename F>
    static void Run(F&& entry, Config config);

    template <typename F>
    static std::unique_ptr<Scheduler> Create(F&& entry);

    template <typename F>
    static std::unique_ptr<Scheduler> Create(F&& entry, Config config);

    static Scheduler& Current();
    static Scheduler* TryCurrent() noexcept;

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&) = delete;
    Scheduler& operator=(Scheduler&&) = delete;
    ~Scheduler();

    void Wait();
    void Stop();

    [[nodiscard]] bool IsDone() const noexcept;
    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] bool IsStopping() const noexcept;
    [[nodiscard]] std::size_t GetWorkerCount() const noexcept;
    [[nodiscard]] std::size_t GetDefaultStackSize() const noexcept;
    [[nodiscard]] bool IsWorkStealingEnabled() const noexcept;
    [[nodiscard]] MemoryResourceSharedPtr GetMemoryResource() const noexcept;

    /// @cond INTERNAL
    detail::Fiber::Id SpawnFiberInternal(detail::Fiber::Body func, std::size_t stack_size);
    /// @endcond

private:
    struct WorkerQueue {
        std::deque<detail::Fiber*> queue;
        std::mutex mutex;
    };

    template <typename T>
    friend class Future;
    template <typename F>
    friend auto Spawn(F&& func) -> Future<std::invoke_result_t<F>>;
    template <typename F>
    friend auto Spawn(F&& func, std::size_t stack_size) -> Future<std::invoke_result_t<F>>;
    friend class Mutex;
    friend class ConditionVariable;
    friend void Yield();
    friend bool YieldIfOthersReady();

    explicit Scheduler(Config config);

    void StartWorkers();
    void WorkerLoop(std::size_t worker_index);
    void RunInlineLoop();
    void HandleFiberCompletion(detail::Fiber* fiber);
    void RemoveFiber(detail::Fiber::Id id);

    void EnqueueReady(detail::Fiber* fiber, std::optional<std::size_t> preferred_worker = std::nullopt);
    bool TryPopLocal(std::size_t worker_index, detail::Fiber*& out_fiber);
    bool TrySteal(std::size_t worker_index, detail::Fiber*& out_fiber);

    [[nodiscard]] std::size_t PickWorker(std::optional<std::size_t> preferred_worker) noexcept;
    [[nodiscard]] std::optional<std::size_t> CurrentWorkerIndex() const noexcept;
    [[nodiscard]] detail::Fiber* GetCurrentFiber() const noexcept;

    void YieldCurrent();
    [[nodiscard]] bool HasOtherReadyFibers() const noexcept;

private:
    Config config_;
    std::size_t worker_count_ {1};
    bool use_inline_worker_ {false};

    std::atomic<bool> running_ {false};
    std::atomic<bool> stopping_ {false};
    std::atomic<std::size_t> ready_count_ {0};
    std::atomic<std::size_t> active_fibers_ {0};
    std::atomic<std::size_t> round_robin_worker_ {0};

    std::vector<std::unique_ptr<WorkerQueue>> worker_queues_;
    std::vector<std::thread> workers_;

    std::mutex work_mutex_;
    std::condition_variable work_cv_;

    std::mutex done_mutex_;
    std::condition_variable done_cv_;

    std::mutex fibers_mutex_;
    std::unordered_map<detail::Fiber::Id, std::unique_ptr<detail::Fiber>> fibers_;
};

template <typename F>
void Scheduler::Run(F&& entry) {
    Run(std::forward<F>(entry), Config {});
}

template <typename F>
void Scheduler::Run(F&& entry, Config config) {
    auto scheduler = Create(std::forward<F>(entry), std::move(config));
    scheduler->Wait();
}

template <typename F>
std::unique_ptr<Scheduler> Scheduler::Create(F&& entry) {
    return Create(std::forward<F>(entry), Config {});
}

template <typename F>
std::unique_ptr<Scheduler> Scheduler::Create(F&& entry, Config config) {
    std::unique_ptr<Scheduler> scheduler(new Scheduler(std::move(config)));
    scheduler->SpawnFiberInternal(std::forward<F>(entry), scheduler->config_.default_stack_size);
    return scheduler;
}

} // namespace cortex::fiber
