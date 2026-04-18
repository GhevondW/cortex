#pragma once

/**
 * @file runtime.hpp
 * @brief Runtime — top-level entry point for the async runtime.
 */

#include <cortex/async/executor.hpp>
#include <cortex/coroutine.hpp>
#include <cortex/memory_resource.hpp>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace cortex::async {

/**
 * @class Runtime
 * @brief Top-level entry point for the multithreaded async runtime.
 *
 * The Runtime manages one or more Executors, each with its own thread pool.
 * A default executor is created automatically. Additional executors can be
 * created for dedicated workloads (IO, background, etc.).
 *
 * ## Quick Start
 *
 * ```cpp
 * #include <cortex/async/async.hpp>
 *
 * int main() {
 *     cortex::async::Runtime::Run([] {
 *         auto task = cortex::async::Spawn([] {
 *             return 42;
 *         });
 *         int result = task.Get();
 *     });
 * }
 * ```
 */
class Runtime final {
public:
    /**
     * @struct Config
     * @brief Configuration for the runtime.
     */
    struct Config {
        std::size_t thread_count = 0; ///< 0 = std::thread::hardware_concurrency()
        std::size_t default_stack_size = cortex::Coroutine::kDefaultStackSizeBytes;
        cortex::MemoryResourceSharedPtr memory_resource = cortex::GetDefaultMemoryResource();
    };

    /**
     * @brief Create and start a runtime with default configuration.
     */
    static std::unique_ptr<Runtime> Create();

    /**
     * @brief Create and start a runtime with custom configuration.
     */
    static std::unique_ptr<Runtime> Create(Config config);

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime&&) = delete;
    ~Runtime();

    /**
     * @brief Access the default (general-purpose) executor.
     */
    [[nodiscard]] Executor& GetDefaultExecutor() noexcept;

    /**
     * @brief Access a named executor, or nullptr if not found.
     */
    [[nodiscard]] Executor* GetExecutor(std::string_view name) noexcept;

    /**
     * @brief Create an additional executor with the given name and thread count.
     * @throws std::runtime_error if the name already exists.
     */
    Executor& CreateExecutor(std::string_view name, std::size_t thread_count);

    /**
     * @brief Number of worker threads across all executors.
     */
    [[nodiscard]] std::size_t GetTotalThreadCount() const noexcept;

    /**
     * @brief Get the runtime configuration.
     */
    [[nodiscard]] const Config& GetConfig() const noexcept;

    /**
     * @brief Initiate graceful shutdown.
     *
     * Signals all executors to stop accepting new work. Blocks until
     * all fibers complete or are cancelled.
     */
    void Shutdown();

    /**
     * @brief Check if shutdown has been initiated.
     */
    [[nodiscard]] bool IsShuttingDown() const noexcept;

    /**
     * @brief Block calling thread until the runtime finishes.
     */
    void WaitForCompletion();

    /**
     * @brief Convenience: create runtime, run entry callable, shut down, return.
     *
     * Similar to tiny_fiber::Scheduler::Run but multithreaded.
     */
    template <typename F>
    static void Run(F&& entry);

    /**
     * @brief Convenience with custom configuration.
     */
    template <typename F>
    static void Run(F&& entry, Config config);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit Runtime(std::unique_ptr<Impl> impl);
};

// Template implementations

template <typename F>
void Runtime::Run([[maybe_unused]] F&& entry) {
    Run(std::forward<F>(entry), Config {});
}

template <typename F>
void Runtime::Run([[maybe_unused]] F&& entry, [[maybe_unused]] Config config) {
    throw std::runtime_error("Not implemented yet");
}

} // namespace cortex::async
