#pragma once

namespace cortex::detail {

/**
 * @brief Internal exception used to force stack unwinding when a coroutine is destroyed before finishing.
 */
struct ForcedUnwind final {};

} // namespace cortex::detail
