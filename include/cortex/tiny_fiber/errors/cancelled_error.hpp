#pragma once

#include <stdexcept>
#include <string>

/**
 * @file cancelled_error.hpp
 * @brief Exception thrown at cancellation points of a cancelled fiber.
 */

namespace cortex::tiny_fiber {

/**
 * @class CancelledError
 * @brief Thrown inside a fiber that must stop: its Future was cancelled, or
 *        (as the derived SchedulerStoppingError) its scheduler is stopping.
 *
 * It is thrown only at suspension points (Yield, CheckPoint, sleeping,
 * waiting on a Future or ConditionVariable, channel operations), so code
 * between two such points always runs to completion. Letting it escape the
 * fiber is the normal way to finish: the fiber's Future then reports it, and
 * it is never treated as an unhandled error.
 */
class CancelledError : public std::runtime_error {
public:
    CancelledError()
        : std::runtime_error("Fiber was cancelled") {}

protected:
    explicit CancelledError(const std::string& message)
        : std::runtime_error(message) {}
};

} // namespace cortex::tiny_fiber
