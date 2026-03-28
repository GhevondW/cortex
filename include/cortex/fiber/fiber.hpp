#pragma once

/**
 * @file fiber.hpp
 * @brief Convenience header for the native multithreaded fiber module.
 */

#include <cortex/fiber/condition_variable.hpp>
#include <cortex/fiber/detail/platform.hpp>
#include <cortex/fiber/errors/scheduler_stopping_error.hpp>
#include <cortex/fiber/future.hpp>
#include <cortex/fiber/mutex.hpp>
#include <cortex/fiber/scheduler.hpp>
#include <cortex/fiber/yield.hpp>

/**
 * @namespace cortex::fiber
 * @brief Multithreaded cooperative fibers for native builds.
 *
 * This module extends @ref cortex::tiny_fiber with a worker-thread scheduler
 * suitable for CPU-parallel workloads.
 *
 * @note @ref cortex::fiber is not available on WebAssembly.
 * Use @ref cortex::tiny_fiber for WASM clients.
 *
 * ## Quick Start
 * @code
 * #include <cortex/fiber/fiber.hpp>
 *
 * int main() {
 *     cortex::fiber::Scheduler::Run([] {
 *         auto future = cortex::fiber::Spawn([] {
 *             return 42;
 *         });
 *
 *         int result = future.Get();
 *     });
 *     return 0;
 * }
 * @endcode
 *
 * ## Components
 * - @b Scheduler: manages fibers across worker threads
 * - @b Future<T> and @b Spawn(): asynchronous task result handling
 * - @b Yield() / @b YieldIfOthersReady(): cooperative scheduling points
 * - @b Mutex and @b ConditionVariable: fiber-aware synchronization
 */
namespace cortex::fiber {
// Convenience umbrella header.
}
