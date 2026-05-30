#pragma once

namespace cortex::detail {

/**
 * @brief Internal sentinel exception used to force stack unwinding when a
 *        coroutine is destroyed before finishing.
 *
 * Thrown from a coroutine's suspend point during destruction and caught only at
 * the coroutine boundary (coroutine_*_impl.cpp). It must propagate uncaught
 * through intermediate fiber/Spawn bodies — any `catch (...)` in between must
 * rethrow it rather than capture it, otherwise the unwind is corrupted and the
 * internal type can leak into user code via a Future.
 */
struct ForcedUnwind final {};

} // namespace cortex::detail
