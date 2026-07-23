#pragma once

/**
 * @file null_mutex.hpp
 * @brief No-op mutex for single-threaded pool instantiations.
 */

namespace cortex::detail {

/**
 * @struct NullMutex
 * @brief Satisfies the Lockable concept with no-ops.
 *
 * Used via std::conditional_t to compile the locking out of
 * single-threaded pool instantiations entirely.
 */
struct NullMutex {
    void lock() noexcept {}
    void unlock() noexcept {}
    bool try_lock() noexcept {
        return true;
    }
};

} // namespace cortex::detail
