#pragma once

#include <stdexcept>

/**
 * @file runtime_shutdown_error.hpp
 * @brief Exception thrown when operations are attempted on a shutting-down runtime.
 */

namespace cortex::async {

/**
 * @class RuntimeShutdownError
 * @brief Exception thrown when the runtime is shutting down.
 *
 * This exception is thrown by Yield(), sync primitives, and other
 * blocking operations when the runtime is being shut down. Fibers
 * should catch this to clean up gracefully.
 */
class RuntimeShutdownError : public std::runtime_error {
public:
    RuntimeShutdownError()
        : std::runtime_error("Runtime is shutting down") {}
};

} // namespace cortex::async
