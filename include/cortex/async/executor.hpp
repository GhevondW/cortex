#pragma once

/**
 * @file executor.hpp
 * @brief Executor — thread pool that schedules and runs fibers.
 */

#include <cortex/async/task.hpp>
#include <cortex/memory_resource.hpp>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace cortex::async {

/**
 * @enum SchedulerPolicy
 * @brief Scheduling strategy for distributing fibers across threads.
 */
enum class SchedulerPolicy : std::uint8_t {
    kWorkStealing, ///< Threads steal from each other's queues (default)
    kRoundRobin, ///< Tasks distributed round-robin to threads
    kPinned, ///< Tasks pinned to the submitting thread
};

/**
 * @class Executor
 * @brief Manages a pool of OS threads that run fibers.
 *
 * Each Executor owns a set of worker threads. Fibers are scheduled
 * onto these threads according to the configured SchedulerPolicy.
 * The default policy is work-stealing.
 *
 * Executor is polymorphic to allow future specializations (e.g., GPU executor).
 */
class Executor {
public:
    /**
     * @struct Config
     * @brief Configuration for an executor.
     */
    struct Config {
        std::size_t thread_count = 0; ///< 0 = std::thread::hardware_concurrency()
        std::size_t default_stack_size = 262144; ///< 256KB
        SchedulerPolicy policy = SchedulerPolicy::kWorkStealing;
        cortex::MemoryResourceSharedPtr memory_resource = cortex::GetDefaultMemoryResource();
    };

    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;
    virtual ~Executor();

    /**
     * @brief Spawn a fiber on this executor. Returns a Task handle.
     */
    template <typename F>
    auto Spawn([[maybe_unused]] F&& func) -> Task<std::invoke_result_t<F>> {
        throw std::runtime_error("Not implemented yet");
    }

    /**
     * @brief Spawn with explicit stack size.
     */
    template <typename F>
    auto Spawn([[maybe_unused]] F&& func, [[maybe_unused]] std::size_t stack_size) -> Task<std::invoke_result_t<F>> {
        throw std::runtime_error("Not implemented yet");
    }

    /**
     * @brief Spawn a fire-and-forget fiber (no Task returned).
     */
    template <typename F>
    void SpawnDetached([[maybe_unused]] F&& func) {
        throw std::runtime_error("Not implemented yet");
    }

    /**
     * @brief Spawn fire-and-forget with explicit stack size.
     */
    template <typename F>
    void SpawnDetached([[maybe_unused]] F&& func, [[maybe_unused]] std::size_t stack_size) {
        throw std::runtime_error("Not implemented yet");
    }

    /**
     * @brief Name of this executor (e.g., "default", "io", "background").
     */
    [[nodiscard]] std::string_view GetName() const noexcept;

    /**
     * @brief Number of worker threads in this executor.
     */
    [[nodiscard]] std::size_t GetThreadCount() const noexcept;

    /**
     * @brief Default fiber stack size.
     */
    [[nodiscard]] std::size_t GetDefaultStackSize() const noexcept;

    /**
     * @brief Scheduling policy.
     */
    [[nodiscard]] SchedulerPolicy GetPolicy() const noexcept;

    /**
     * @brief Number of fibers currently alive on this executor.
     */
    [[nodiscard]] std::size_t GetActiveFiberCount() const noexcept;

    /**
     * @brief Number of fibers in ready queues (pending execution).
     */
    [[nodiscard]] std::size_t GetPendingFiberCount() const noexcept;

    /**
     * @brief Whether this executor is accepting new work.
     */
    [[nodiscard]] bool IsRunning() const noexcept;

protected:
    Executor();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend class Runtime;
};

} // namespace cortex::async
