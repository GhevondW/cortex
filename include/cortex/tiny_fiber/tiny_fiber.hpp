#pragma once

/**
 * @file tiny_fiber.hpp
 * @brief Convenience header that includes all tiny_fiber components.
 */

#include <cortex/tiny_fiber/condition_variable.hpp>
#include <cortex/tiny_fiber/errors/scheduler_stopping_error.hpp>
#include <cortex/tiny_fiber/future.hpp>
#include <cortex/tiny_fiber/mutex.hpp>
#include <cortex/tiny_fiber/scheduler.hpp>
#include <cortex/tiny_fiber/yield.hpp>

/**
 * @namespace cortex::tiny_fiber
 * @brief Cooperative multitasking primitives built on cortex::Coroutine.
 *
 * This module provides fiber-based cooperative multitasking that works
 * on both native and WebAssembly platforms without any threading.
 *
 * ## Quick Start
 *
 * ```cpp
 * #include <cortex/tiny_fiber/tiny_fiber.hpp>
 *
 * int main() {
 *     cortex::tiny_fiber::Scheduler::Run([] {
 *         auto future = cortex::tiny_fiber::Spawn([] {
 *             cortex::tiny_fiber::Yield();
 *             return 42;
 *         });
 *
 *         int result = future.Get();
 *     });
 *     return 0;
 * }
 * ```
 *
 * ## Components
 *
 * - **Scheduler**: Manages fiber execution
 * - **Future<T>**: Handle to a spawned fiber's result
 * - **Spawn()**: Create new fibers
 * - **Yield()**: Cooperative yielding
 * - **Mutex**: Cooperative locking
 * - **ConditionVariable**: Cooperative waiting
 */

namespace cortex::tiny_fiber {
// All types are defined in individual headers
}
