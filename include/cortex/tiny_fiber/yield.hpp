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
 */
void Yield();

/**
 * @brief Yield only if there are other ready fibers.
 *
 * @return true if yielded, false if no other fibers are ready.
 * @throws std::logic_error if called outside of a fiber.
 */
bool YieldIfOthersReady();

} // namespace cortex::tiny_fiber
