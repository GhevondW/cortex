#pragma once

#include <cortex/memory_resource.hpp>

#include <cstddef>

/**
 * @file guarded_stack_resource.hpp
 * @brief Memory resource that puts a guard page under every stack.
 */

namespace cortex {

/**
 * @brief Whether MakeGuardedStackResource() really installs guard pages on
 *        this platform (POSIX native builds). Elsewhere it returns a resource
 *        that simply forwards to its upstream.
 */
[[nodiscard]] bool HasGuardPageSupport() noexcept;

/**
 * @struct GuardedStackResourceConfig
 * @brief Options for MakeGuardedStackResource().
 */
struct GuardedStackResourceConfig {
    /// Requests at least this large are treated as stacks and get a guard
    /// page; smaller ones (fiber objects, future states) go to `upstream`.
    std::size_t min_guarded_bytes = 16u * 1024u;
    /// Resource for allocations below `min_guarded_bytes`.
    MemoryResourceSharedPtr upstream = GetDefaultMemoryResource();
};

/**
 * @brief Create a resource whose stack-sized blocks sit directly above an
 *        inaccessible guard page.
 *
 * Stacks grow down, so a fiber that overflows its stack touches the guard
 * page and the process faults right there, instead of silently corrupting
 * whatever memory lies below the stack. Blocks come straight from the OS
 * (mmap), rounded up to whole pages. Combine with PooledMemoryResource (as
 * the tiny_fiber scheduler does by default) so recycled stacks keep their
 * guard page and spawning stays cheap.
 */
MemoryResourceSharedPtr MakeGuardedStackResource(GuardedStackResourceConfig config = {});

} // namespace cortex
