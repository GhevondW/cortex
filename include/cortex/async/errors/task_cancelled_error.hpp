#pragma once

#include <stdexcept>

/**
 * @file task_cancelled_error.hpp
 * @brief Exception thrown when a cancelled task's result is accessed.
 */

namespace cortex::async {

/**
 * @class TaskCancelledError
 * @brief Exception thrown when a task was cancelled.
 *
 * Thrown by Task::Get() and Task::Wait() when the underlying fiber
 * was cancelled via Task::Cancel().
 */
class TaskCancelledError : public std::runtime_error {
public:
    TaskCancelledError()
        : std::runtime_error("Task was cancelled") {}
};

} // namespace cortex::async
