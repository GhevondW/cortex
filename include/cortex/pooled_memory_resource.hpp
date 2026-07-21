#pragma once

#include <cstddef>
#include <unordered_map>
#include <vector>

#include <cortex/memory_resource.hpp>

/**
 * @file pooled_memory_resource.hpp
 * @brief Memory resource that recycles coroutine stacks.
 */

namespace cortex {

/**
 * @class PooledMemoryResource
 * @brief A MemoryResource that free-lists blocks by size for fast reuse.
 *
 * Coroutine creation is dominated by the stack allocation (256KB by
 * default). This resource keeps deallocated blocks in per-size free lists
 * so that spawning a new coroutine or fiber reuses a previous stack
 * instead of going back to the upstream allocator. Reused stacks also keep
 * their pages warm, avoiding page faults on first touch.
 *
 * Cached memory is bounded by Config::max_cached_bytes; blocks released
 * beyond the cap are returned to the upstream resource immediately. All
 * cached blocks are released to the upstream resource on destruction.
 *
 * @note Not thread-safe. Use one instance per thread (the tiny_fiber
 * Scheduler creates one per scheduler by default, which is safe because a
 * scheduler and its fibers live on a single thread).
 */
class PooledMemoryResource final : public MemoryResource {
public:
    /**
     * @struct Config
     * @brief Configuration options for the pool.
     */
    struct Config {
        /// Upper bound for bytes kept alive in the free lists.
        std::size_t max_cached_bytes = 64u * 1024u * 1024u;
        /// Resource used for actual allocations on pool misses.
        MemoryResourceSharedPtr upstream = GetDefaultMemoryResource();
    };

    PooledMemoryResource();
    explicit PooledMemoryResource(Config config);

    PooledMemoryResource(const PooledMemoryResource&) = delete;
    PooledMemoryResource(PooledMemoryResource&&) = delete;
    PooledMemoryResource& operator=(const PooledMemoryResource&) = delete;
    PooledMemoryResource& operator=(PooledMemoryResource&&) = delete;

    ~PooledMemoryResource() override;

    /**
     * @brief Returns the number of bytes currently held in the free lists.
     */
    [[nodiscard]] std::size_t GetCachedBytes() const noexcept {
        return cached_bytes_;
    }

private:
    void* DoAllocate(std::size_t bytes, std::size_t alignment) override;
    void DoDeallocate(void* p, std::size_t bytes, std::size_t alignment) override;

private:
    Config config_;
    std::size_t cached_bytes_ {0};
    std::unordered_map<std::size_t, std::vector<void*>> free_lists_;
};

/**
 * @brief Creates a PooledMemoryResource with default configuration.
 */
MemoryResourceSharedPtr MakePooledMemoryResource();

/**
 * @brief Creates a PooledMemoryResource with the given configuration.
 */
MemoryResourceSharedPtr MakePooledMemoryResource(PooledMemoryResource::Config config);

} // namespace cortex
