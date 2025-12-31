#pragma once

#include <function2/function2.hpp>

#include <cortex/coroutine_suspend_context.hpp>

/**
 * @file coroutine_body.hpp
 * @brief Definition of the coroutine execution body.
 */

namespace cortex {

/**
 * @typedef CoroutineBody
 * @brief The signature for a coroutine's entry point.
 *
 * A coroutine body is a callable that receives a CoroutineSuspendContext
 * reference, which it can use to suspend its execution.
 */
using CoroutineBody = fu2::unique_function<void(CoroutineSuspendContext&)>;

} // namespace cortex
