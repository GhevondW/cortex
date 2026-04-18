#pragma once

/**
 * @file async.hpp
 * @brief Convenience header that includes all cortex::async components.
 */

#include <cortex/async/errors/channel_closed_error.hpp>
#include <cortex/async/errors/no_executor_error.hpp>
#include <cortex/async/errors/runtime_shutdown_error.hpp>
#include <cortex/async/errors/task_cancelled_error.hpp>
#include <cortex/async/executor.hpp>
#include <cortex/async/future.hpp>
#include <cortex/async/runtime.hpp>
#include <cortex/async/spawn.hpp>
#include <cortex/async/task.hpp>

#include <cortex/async/sync/baton.hpp>
#include <cortex/async/sync/condition_variable.hpp>
#include <cortex/async/sync/event.hpp>
#include <cortex/async/sync/mutex.hpp>
#include <cortex/async/sync/semaphore.hpp>
#include <cortex/async/sync/shared_mutex.hpp>
#include <cortex/async/sync/wait_group.hpp>

#include <cortex/async/channel/channel.hpp>

/**
 * @namespace cortex::async
 * @brief Multithreaded async runtime built on cortex::Coroutine.
 *
 * This module provides a fiber-based concurrent programming framework
 * with work-stealing scheduler, fiber-aware synchronization primitives,
 * and CSP-style channels.
 *
 * ## Quick Start
 *
 * ```cpp
 * #include <cortex/async/async.hpp>
 *
 * namespace ca = cortex::async;
 *
 * int main() {
 *     ca::Runtime::Run([] {
 *         auto task = ca::Spawn([] {
 *             ca::Yield();
 *             return 42;
 *         });
 *
 *         int result = task.Get();
 *     });
 * }
 * ```
 *
 * ## Modules
 *
 * - **Runtime**: Top-level entry point, manages thread pool
 * - **Executor**: Thread pool with configurable scheduling policy
 * - **Task<T>**: Handle to a spawned fiber's result
 * - **Future<T> / Promise<T>**: Async value passing between fibers
 * - **Spawn / Yield / SleepFor**: Free functions for fiber control
 * - **sync::Mutex, Baton, Semaphore, ...**: Fiber-aware synchronization
 * - **channel::Channel<T>**: CSP-style inter-fiber communication
 */

namespace cortex::async {
// All types are defined in individual headers
} // namespace cortex::async
