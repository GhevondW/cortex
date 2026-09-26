#pragma once

/**
 * @file tiny_fiber.hpp
 * @brief Convenience header that includes all tiny_fiber components.
 */

#include <cortex/tiny_fiber/channel.hpp>
#include <cortex/tiny_fiber/condition_variable.hpp>
#include <cortex/tiny_fiber/errors/broken_promise_error.hpp>
#include <cortex/tiny_fiber/errors/cancelled_error.hpp>
#include <cortex/tiny_fiber/errors/deadlock_error.hpp>
#include <cortex/tiny_fiber/errors/scheduler_stopping_error.hpp>
#include <cortex/tiny_fiber/future.hpp>
#include <cortex/tiny_fiber/mutex.hpp>
#include <cortex/tiny_fiber/promise.hpp>
#include <cortex/tiny_fiber/scheduler.hpp>
#include <cortex/tiny_fiber/wait.hpp>
#include <cortex/tiny_fiber/yield.hpp>

/**
 * @namespace cortex::tiny_fiber
 * @brief Cooperative multitasking primitives built on cortex::Coroutine.
 *
 * This module provides fiber-based cooperative multitasking that works
 * on both native and WebAssembly platforms without any threading. In the
 * browser, drive a scheduler with js/cortex.mjs (see Scheduler::RunFor()).
 *
 * ## Quick Start
 *
 * ```cpp
 * #include <cortex/tiny_fiber/tiny_fiber.hpp>
 *
 * int main() {
 *     const int result = cortex::tiny_fiber::Scheduler::Run([] {
 *         auto future = cortex::tiny_fiber::Spawn([] {
 *             cortex::tiny_fiber::Yield();
 *             return 42;
 *         });
 *         return future.Get();
 *     });
 *     return result == 42 ? 0 : 1;
 * }
 * ```
 *
 * ## Components
 *
 * - **Scheduler**: runs fibers; Run(), or Create() + RunFor()/Step(); status,
 *   timers, Post(), wake-up handler, deadlock reports
 * - **Spawn() / SpawnDetached() / Future<T>**: start fibers, wait, time out,
 *   cancel, detach
 * - **Promise<T>**: results delivered from outside fibers
 * - **WaitAll() / WaitAny()**: wait on several futures
 * - **Channel<T>**: FIFO message passing
 * - **CheckPoint() / Yield() / SleepFor()**: cooperative slicing, yields, timers
 * - **Mutex / ConditionVariable**: cooperative synchronization
 * - **CancelledError / SchedulerStoppingError / DeadlockError / BrokenPromiseError**
 */

namespace cortex::tiny_fiber {
// All types are defined in individual headers
}
