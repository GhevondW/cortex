#pragma once

#include <stdexcept>

/**
 * @file resume_on_completed_coroutine_error.hpp
 * @brief Error thrown when attempting to resume a finished coroutine.
 */

namespace cortex {

/**
 * @struct ResumeOnDoneCoroutineError
 * @brief Exception thrown when calling Resume() on a coroutine that has finished execution.
 */
struct ResumeOnDoneCoroutineError : std::logic_error {
    using logic_error::logic_error;
};

} // namespace cortex
