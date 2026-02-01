#pragma once

/**
 * @file yield.hpp
 * @brief Yield functions for cooperative multitasking.
 */

namespace cortex::tiny_fiber {

/**
 * @brief Yield control to other ready fibers.
 *
 * The current fiber is placed at the back of the ready queue.
 * Must be called from within a fiber.
 *
 * @throws std::logic_error if called outside of a fiber.
 * @throws SchedulerStoppingError if the scheduler is stopping.
 */
void Yield();

/**
 * @brief Yield only if there are other ready fibers.
 *
 * @return true if yielded, false if no other fibers are ready.
 * @throws std::logic_error if called outside of a fiber.
 * @throws SchedulerStoppingError if the scheduler is stopping.
 */
bool YieldIfOthersReady();

/**
 * @brief Check if the current scheduler is stopping.
 *
 * Fibers can use this to exit gracefully during shutdown.
 *
 * @return true if the scheduler is stopping, false otherwise.
 * @throws std::logic_error if called outside of a fiber.
 */
bool IsStopping();

} // namespace cortex::tiny_fiber
