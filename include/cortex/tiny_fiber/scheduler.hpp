#pragma once

#include <cortex/memory_resource.hpp>
#include <cortex/pooled_memory_resource.hpp>
#include <cortex/tiny_fiber/detail/fiber.hpp>

#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

/**
 * @file scheduler.hpp
 * @brief Cooperative fiber scheduler for tiny_fiber.
 */

namespace cortex::tiny_fiber {

/**
 * @brief The memory resource a Scheduler uses unless configured otherwise.
 *
 * A per-scheduler pool: fiber stacks are recycled instead of hitting the
 * system allocator on every Spawn. Safe because a scheduler and its fibers
 * live on a single thread.
 */
MemoryResourceSharedPtr MakeDefaultFiberResource();

/**
 * @class Scheduler
 * @brief Manages cooperative execution of fibers.
 *
 * The scheduler maintains a ready queue of fibers and runs them one at a time
 * on the calling thread.
 *
 * Two modes of operation:
 * 1. `Run()` - blocks until all fibers complete (simple usage)
 * 2. `Create()` + `Step()` - manual stepping for WASM/async integration
 *
 * Failures are never silent: an exception escaping a fiber that nobody can
 * observe (the Create() entry, detached fibers) is rethrown by Step()/Run()
 * or passed to Config::on_unhandled_exception, and Run() throws
 * DeadlockError when fibers are left that can never finish.
 */
class Scheduler {
public:
    /**
     * @brief What a scheduler can do next.
     */
    enum class Status : std::uint8_t {
        kRunnable, ///< Fibers are ready to run now: call Step() again.
        kWaiting, ///< Nothing runnable, but fibers wait on timers or external events.
        kDone, ///< Every fiber has finished.
        kDeadlocked, ///< Fibers are suspended and nothing can ever wake them.
    };

    /**
     * @struct Config
     * @brief Configuration options for the scheduler.
     */
    struct Config {
        std::size_t default_stack_size = cortex::Coroutine::kDefaultStackSizeBytes;
        /// Where fibers, their stacks and future states are allocated.
        MemoryResourceSharedPtr memory_resource = MakeDefaultFiberResource();
        /// Receives exceptions that escape fibers nobody observes (the
        /// Create() entry, detached fibers). Called from Step(), outside any
        /// fiber. When empty, Step() / Run() rethrow them instead.
        std::function<void(std::exception_ptr)> on_unhandled_exception;
    };

    /**
     * @brief Run `entry` in a fiber and drive the scheduler until every fiber
     *        has finished.
     *
     * @param entry The function to run in the initial fiber.
     * @return What `entry` returned.
     * @throws Whatever `entry` threw; an unhandled exception from a detached
     *         fiber; DeadlockError if fibers remain that can never finish.
     */
    template <typename F>
    static auto Run(F&& entry) -> std::invoke_result_t<F>;

    /**
     * @brief Run with a custom config. See Run(F&&).
     */
    template <typename F>
    static auto Run(F&& entry, Config config) -> std::invoke_result_t<F>;

    /**
     * @brief Create a scheduler for manual stepping (WASM/async integration).
     *
     * Use Step() to advance the scheduler one fiber at a time.
     * This allows yielding back to JS event loop between fiber switches.
     * An exception escaping `entry` is rethrown by the Step() that ran it.
     *
     * @param entry The function to run in the initial fiber.
     * @return A unique_ptr to the Scheduler instance.
     */
    template <typename F>
    static std::unique_ptr<Scheduler> Create(F&& entry);

    /**
     * @brief Create a scheduler for manual stepping with custom config.
     */
    template <typename F>
    static std::unique_ptr<Scheduler> Create(F&& entry, Config config);

    /**
     * @brief Get the current scheduler.
     *
     * Must be called from within a fiber.
     *
     * @return Reference to the current scheduler.
     * @throws std::logic_error if called outside of a fiber.
     */
    static Scheduler& Current();

    /**
     * @brief Get the innermost scheduler running on this thread, if any.
     *
     * @return The running scheduler, or nullptr outside of fibers.
     */
    [[nodiscard]] static Scheduler* TryCurrent() noexcept;

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&) = delete;
    Scheduler& operator=(Scheduler&&) = delete;
    ~Scheduler();

    /**
     * @brief Run one step of the scheduler.
     *
     * Picks one ready fiber and runs it until it yields, parks or completes.
     *
     * @return true if more fibers are ready to run right now. false means
     *         nothing is runnable: check GetStatus() to tell "done" from
     *         "deadlocked".
     * @throws An exception that escaped a fiber nobody observes, unless
     *         Config::on_unhandled_exception is set.
     */
    bool Step();

    /**
     * @brief What the scheduler can do next (see Status).
     */
    [[nodiscard]] Status GetStatus() const;

    /**
     * @brief Check if every fiber has finished.
     *
     * A scheduler whose fibers are all parked forever is not done: its
     * status is Status::kDeadlocked.
     */
    [[nodiscard]] bool IsDone() const noexcept {
        return live_fibers_ == 0;
    }

    /**
     * @brief Number of fibers that have not finished yet.
     */
    [[nodiscard]] std::size_t GetFiberCount() const noexcept {
        return live_fibers_;
    }

    /**
     * @brief Human-readable list of live fibers: name, state and what each
     *        suspended fiber waits in. Useful when debugging hangs.
     */
    [[nodiscard]] std::string DescribeFibers() const;

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

    /**
     * @brief Check if the scheduler is stopping (being destroyed).
     *
     * Fibers can check this to exit gracefully during shutdown.
     */
    [[nodiscard]] bool IsStopping() const noexcept {
        return stopping_;
    }

    /**
     * @brief Signal all fibers to stop and wake suspended ones.
     *
     * Called automatically during destruction, but can be called
     * manually to initiate graceful shutdown.
     */
    void Stop();

    /// @cond INTERNAL
    // Internal hooks used by tiny_fiber's own primitives (futures, wait
    // queues, sync primitives). Not part of the public API.
    detail::Fiber::Id SpawnFiberInternal(detail::Fiber::Body func, std::size_t stack_size);

    // Currently running fiber of this scheduler, or nullptr.
    [[nodiscard]] detail::Fiber* GetCurrentFiber() noexcept {
        return current_fiber_;
    }

    // Start a wait for the current fiber: returns the token to record in the
    // queue the fiber is about to park on. Throws if no fiber is running.
    detail::WaiterRef PrepareWait();

    // Park the current fiber until something wakes it. `reason` must be a
    // string literal (reported by diagnostics); `cancellable` marks waits
    // that cancellation may interrupt.
    void ParkCurrent(const char* reason, bool cancellable);

    // Wake the fiber `ref` names iff it is still parked in that same wait.
    // Stale tokens (fiber gone, already woken, or parked in a later wait)
    // are ignored. Returns whether a fiber was woken.
    bool WakeIfWaiting(detail::WaiterRef ref);

    // Unconditionally wake a parked fiber and enqueue it to run.
    void WakeFiber(detail::Fiber* fiber);

    // Throws SchedulerStoppingError once the scheduler is stopping. Every
    // suspension point calls this before parking.
    void ThrowIfInterrupted(bool cancellable) const;

    // An exception escaped a fiber nobody observes (a detached fiber or the
    // Create() entry). Delivered by the Step() that ran the fiber.
    void ReportUnhandledInternal(std::exception_ptr ex);

    // Liveness token. A Future holds this weakly so its destructor / Wait / Get
    // can detect that the scheduler has been destroyed and skip dereferencing a
    // dangling pointer (a Future may legally outlive its scheduler).
    [[nodiscard]] std::weak_ptr<void> AliveTokenInternal() const noexcept {
        return alive_token_;
    }
    /// @endcond

private:
    friend class detail::Fiber;
    template <typename T>
    friend class Future;
    friend class Mutex;
    friend class ConditionVariable;
    friend class detail::WaitQueue;
    friend class detail::WaiterList;
    friend void Yield();
    friend bool YieldIfOthersReady();

    explicit Scheduler(Config config);

    // Drive the scheduler until nothing can make progress. Returns kDone or
    // kDeadlocked.
    Status RunToCompletion();

    // The DeadlockError message: explanation plus DescribeFibers().
    [[nodiscard]] std::string DeadlockMessage() const;

    // Hand queued unhandled exceptions to the handler, or rethrow the first.
    void DeliverUnhandled();

    // Get fiber by ID
    detail::Fiber* GetFiber(detail::Fiber::Id id);

    // Yield current fiber (put back in ready queue)
    void YieldCurrent();

    // Check if there are other ready fibers
    bool HasOtherReadyFibers() const;

    // Process pending fiber cleanup
    void ProcessPendingCleanup();

private:
    // Fiber IDs encode a slot index in the low bits and a per-scheduler
    // sequence number in the high bits. Lookup is a bounds-checked index plus
    // an ID comparison, so stale IDs from recycled slots resolve to nullptr —
    // the same liveness guarantee a map lookup gave, without the hashing.
    static constexpr std::uint64_t kSlotIndexBits = 20;
    static constexpr std::uint64_t kSlotIndexMask = (std::uint64_t {1} << kSlotIndexBits) - 1;

    struct FiberSlot {
        detail::Fiber::Id id {0}; // 0 = vacant
        detail::FiberPtr fiber {nullptr, detail::FiberDeleter {nullptr}};
    };

    Config config_;
    bool running_ {false};
    bool stopping_ {false};
    // Sequence counter; starts at 1 so no fiber ID is ever the sentinel 0.
    std::uint64_t next_sequence_ {1};
    std::size_t live_fibers_ {0};
    detail::Fiber* current_fiber_ {nullptr};
    std::deque<detail::Fiber*> ready_queue_;
    std::vector<FiberSlot> fiber_slots_;
    std::vector<std::uint32_t> vacant_slots_;
    std::vector<detail::Fiber::Id> pending_cleanup_;
    std::vector<std::exception_ptr> unhandled_;
    // Owned liveness token; weak copies in Futures expire when this scheduler is
    // destroyed. Declared last so it outlives the other members during teardown.
    std::shared_ptr<void> alive_token_ {std::make_shared<char>()};
};

} // namespace cortex::tiny_fiber

// Scheduler's template members need the fiber-body helpers declared in
// future_state.hpp, which in turn needs the complete Scheduler class above.
// Their definitions live at the end of that header; including it here keeps
// either include order valid.
#include <cortex/tiny_fiber/detail/future_state.hpp>
