#pragma once

#include <stdexcept>

/**
 * @file no_executor_error.hpp
 * @brief Exception thrown when fiber-only operations are called outside a fiber.
 */

namespace cortex::async {

/**
 * @class NoExecutorError
 * @brief Exception thrown when no executor context is available.
 *
 * Operations like Yield(), CurrentExecutor(), and sync primitive waits
 * require an active fiber context. This exception is thrown when they
 * are called from outside a fiber.
 */
class NoExecutorError : public std::logic_error {
public:
    NoExecutorError()
        : std::logic_error("No executor context. This function must be called from within a fiber.") {}
};

} // namespace cortex::async
