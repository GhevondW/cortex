#pragma once

#include <cortex/memory_resource.hpp>
#include <cortex/tiny_fiber/detail/fiber.hpp>

#include <deque>
#include <functional>
#include <memory>
#include <unordered_map>

/**
 * @file scheduler.hpp
 * @brief Cooperative fiber scheduler for tiny_fiber.
 */

namespace cortex::tiny_fiber {

/**
 * @class Scheduler
 * @brief Manages cooperative execution of fibers.
 *
 * The scheduler maintains a ready queue of fibers and runs them
 * one at a time until all fibers complete.
 *
 * Two modes of operation:
 * 1. `Run()` - blocks until all fibers complete (simple usage)
 * 2. `Create()` + `Step()` - manual stepping for WASM/async integration
 */
class Scheduler {
public:
    /**
     * @struct Config
     * @brief Configuration options for the scheduler.
     */
    struct Config {
        std::size_t default_stack_size = cortex::Coroutine::kDefaultStackSizeBytes;
        MemoryResourceSharedPtr memory_resource = GetDefaultMemoryResource();
    };

    /**
     * @brief Run the scheduler with an initial fiber.
     *
     * Blocks until all fibers complete.
     *
     * @param entry The function to run in the initial fiber.
     */
    template <typename F>
    static void Run(F&& entry);

    /**
     * @brief Run the scheduler with an initial fiber and custom config.
     *
     * @param entry The function to run in the initial fiber.
     * @param config Configuration options.
     */
    template <typename F>
    static void Run(F&& entry, Config config);

    /**
     * @brief Create a scheduler for manual stepping (WASM/async integration).
     *
     * Use Step() to advance the scheduler one fiber at a time.
     * This allows yielding back to JS event loop between fiber switches.
     *
     * @param entry The function to run in the initial fiber.
     * @return A Scheduler instance to step through.
     */
    template <typename F>
    static Scheduler Create(F&& entry);

    /**
     * @brief Create a scheduler for manual stepping with custom config.
     */
    template <typename F>
    static Scheduler Create(F&& entry, Config config);

    /**
     * @brief Get the current scheduler.
     *
     * Must be called from within a fiber.
     *
     * @return Reference to the current scheduler.
     * @throws std::logic_error if called outside of a fiber.
     */
    static Scheduler& Current();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&& other) noexcept;
    Scheduler& operator=(Scheduler&& other) noexcept;
    ~Scheduler() = default;

    /**
     * @brief Run one step of the scheduler.
     *
     * Picks one ready fiber and runs it until it yields or completes.
     * Use this for WASM integration with JS event loop.
     *
     * @return true if there's more work to do, false if all fibers are done.
     */
    bool Step();

    /**
     * @brief Check if all fibers have completed.
     */
    [[nodiscard]] bool IsDone() const noexcept {
        return ready_queue_.empty() && !current_fiber_;
    }

    /**
     * @brief Get the default stack size for new fibers.
     */
    [[nodiscard]] std::size_t GetDefaultStackSize() const noexcept {
        return config_.default_stack_size;
    }

    /**
     * @brief Get the memory resource used by this scheduler.
     */
    [[nodiscard]] MemoryResourceSharedPtr GetMemoryResource() const noexcept {
        return config_.memory_resource;
    }

    /**
     * @brief Check if the scheduler is currently running.
     */
    [[nodiscard]] bool IsRunning() const noexcept {
        return running_;
    }

    // Internal API - used by Spawn template function
    // Not intended for direct use
    detail::Fiber::Id SpawnFiberInternal(std::function<void()> func, std::size_t stack_size);

private:
    friend class detail::Fiber;
    template <typename T>
    friend class Future;
    friend class Mutex;
    friend class ConditionVariable;
    friend void Yield();
    friend bool YieldIfOthersReady();

    explicit Scheduler(Config config);

    void RunLoop();

    // Get fiber by ID
    detail::Fiber* GetFiber(detail::Fiber::Id id);

    // Get currently running fiber
    detail::Fiber* GetCurrentFiber() {
        return current_fiber_;
    }

    // Schedule a fiber to run
    void Schedule(detail::Fiber* fiber);

    // Suspend current fiber
    void SuspendCurrent();

    // Yield current fiber (put back in ready queue)
    void YieldCurrent();

    // Check if there are other ready fibers
    bool HasOtherReadyFibers() const;

private:
    Config config_;
    bool running_ {false};
    detail::Fiber::Id next_fiber_id_ {1};
    detail::Fiber* current_fiber_ {nullptr};
    std::deque<detail::Fiber*> ready_queue_;
    std::unordered_map<detail::Fiber::Id, std::unique_ptr<detail::Fiber>> fibers_;
};

// Template implementations
template <typename F>
void Scheduler::Run(F&& entry) {
    Run(std::forward<F>(entry), Config {});
}

template <typename F>
void Scheduler::Run(F&& entry, Config config) {
    Scheduler scheduler(std::move(config));
    scheduler.SpawnFiberInternal(std::forward<F>(entry), scheduler.config_.default_stack_size);
    scheduler.RunLoop();
}

template <typename F>
Scheduler Scheduler::Create(F&& entry) {
    return Create(std::forward<F>(entry), Config {});
}

template <typename F>
Scheduler Scheduler::Create(F&& entry, Config config) {
    Scheduler scheduler(std::move(config));
    scheduler.SpawnFiberInternal(std::forward<F>(entry), scheduler.config_.default_stack_size);
    // Don't set running_ here - Step() will handle it
    return scheduler;
}

} // namespace cortex::tiny_fiber
