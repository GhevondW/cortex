#pragma once

#include <stdexcept>

/**
 * @file broken_promise_error.hpp
 * @brief Exception stored in a Future whose producer disappeared.
 */

namespace cortex::tiny_fiber {

/**
 * @class BrokenPromiseError
 * @brief The fiber or Promise that should have produced a Future's result was
 *        destroyed before doing so. Rethrown by Future::Get()/Wait().
 */
class BrokenPromiseError : public std::runtime_error {
public:
    BrokenPromiseError()
        : std::runtime_error("The producer of this result was destroyed before producing it") {}
};

} // namespace cortex::tiny_fiber
