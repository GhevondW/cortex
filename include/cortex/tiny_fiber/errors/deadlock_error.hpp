#pragma once

#include <stdexcept>
#include <string>

/**
 * @file deadlock_error.hpp
 * @brief Exception thrown when fibers can never finish.
 */

namespace cortex::tiny_fiber {

/**
 * @class DeadlockError
 * @brief Thrown by Scheduler::Run() when every remaining fiber is suspended
 *        and nothing can wake any of them.
 *
 * The message lists the stuck fibers (name, and what each one waits in), as
 * produced by Scheduler::DescribeFibers(). Name fibers with SetFiberName()
 * to make it easier to read.
 */
class DeadlockError : public std::runtime_error {
public:
    explicit DeadlockError(const std::string& message)
        : std::runtime_error(message) {}
};

} // namespace cortex::tiny_fiber
