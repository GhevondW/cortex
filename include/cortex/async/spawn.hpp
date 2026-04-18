#pragma once

/**
 * @file spawn.hpp
 * @brief Free functions for spawning fibers and controlling execution.
 */

#include <cortex/async/task.hpp>

#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <type_traits>

namespace cortex::async {

class Executor;

// --- Spawn functions ---

/**
 * @brief Spawn a fiber on the current executor.
 *
 * Must be called from within a fiber.
 * @throws NoExecutorError if called outside a fiber.
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
 * @brief Spawn on a specific executor.
 */
template <typename F>
auto SpawnOn([[maybe_unused]] Executor& executor, [[maybe_unused]] F&& func) -> Task<std::invoke_result_t<F>> {
    throw std::runtime_error("Not implemented yet");
}

/**
 * @brief Spawn fire-and-forget on current executor.
 */
template <typename F>
void SpawnDetached([[maybe_unused]] F&& func) {
    throw std::runtime_error("Not implemented yet");
}

// --- Yield and sleep ---

/**
 * @brief Yield the current fiber. Another ready fiber runs.
 * @throws NoExecutorError if called outside a fiber.
 */
void Yield();

/**
 * @brief Yield only if there are other ready fibers.
 * @return true if yielded, false if no other fibers were ready.
 */
bool YieldIfOthersReady();

/**
 * @brief Suspend the current fiber for the given duration.
 */
template <typename Rep, typename Period>
void SleepFor([[maybe_unused]] std::chrono::duration<Rep, Period> duration) {
    throw std::runtime_error("Not implemented yet");
}

/**
 * @brief Suspend until a time point.
 */
template <typename Clock, typename Duration>
void SleepUntil([[maybe_unused]] std::chrono::time_point<Clock, Duration> time_point) {
    throw std::runtime_error("Not implemented yet");
}

// --- Context queries ---

/**
 * @brief Get the executor the current fiber is running on.
 * @throws NoExecutorError if called outside a fiber.
 */
Executor& CurrentExecutor();

/**
 * @brief Check if the current fiber has been requested to cancel.
 */
bool IsCancellationRequested();

/**
 * @brief Check if the runtime is shutting down.
 */
bool IsShuttingDown();

// --- Multi-task waiting ---

/**
 * @brief Wait for all tasks. Suspends current fiber until all complete.
 */
template <typename... Tasks>
void WaitAll([[maybe_unused]] Tasks&... tasks) {
    throw std::runtime_error("Not implemented yet");
}

/**
 * @brief Wait for any task. Returns index of the first completed task.
 */
template <typename... Tasks>
std::size_t WaitAny([[maybe_unused]] Tasks&... tasks) {
    throw std::runtime_error("Not implemented yet");
}

} // namespace cortex::async
