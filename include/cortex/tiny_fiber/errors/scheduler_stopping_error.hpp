#pragma once

#include <cortex/tiny_fiber/errors/cancelled_error.hpp>

/**
 * @file scheduler_stopping_error.hpp
 * @brief Exception thrown when operations are attempted on a stopping scheduler.
 */

namespace cortex::tiny_fiber {

/**
 * @class SchedulerStoppingError
 * @brief Exception thrown when the scheduler is stopping.
 *
 * Thrown by every suspension point (Yield(), Mutex::Lock(),
 * ConditionVariable::Wait(), Future::Wait()/Get(), ...) once Stop() was
 * called or the scheduler is being destroyed. It is a CancelledError: stopping
 * a scheduler cancels all of its fibers. Fibers may catch it to clean up.
 *
 * Example:
 * @code
 * try {
 *     while (true) {
 *         do_work();
 *         tf::Yield();
 *     }
 * } catch (const tf::SchedulerStoppingError&) {
 *     // Clean up and exit
 * }
 * @endcode
 */
class SchedulerStoppingError : public CancelledError {
public:
    SchedulerStoppingError()
        : CancelledError("Scheduler is stopping") {}
};

} // namespace cortex::tiny_fiber
