#pragma once

#include <stdexcept>

/**
 * @file scheduler_stopping_error.hpp
 * @brief Exception thrown when operations are attempted on a stopping scheduler.
 */

namespace cortex::tiny_fiber {

/**
 * @class SchedulerStoppingError
 * @brief Exception thrown when the scheduler is stopping.
 *
 * This exception is thrown by Yield(), Mutex::Lock(), and ConditionVariable::Wait()
 * when the scheduler is being destroyed. Fibers should catch this to clean up
 * gracefully.
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
class SchedulerStoppingError : public std::runtime_error {
public:
    SchedulerStoppingError()
        : std::runtime_error("Scheduler is stopping") {}
};

} // namespace cortex::tiny_fiber
