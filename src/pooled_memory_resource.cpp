#include <cortex/pooled_memory_resource.hpp>

#include <utility>

// AddressSanitizer poisons redzones inside a fiber's stack while it runs.
// malloc/free would reset that shadow state, but recycled blocks skip the
// allocator, so they must be unpoisoned explicitly before reuse — otherwise
// the next fiber starts executing on memory ASan still considers poisoned.
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define CORTEX_POOL_HAS_ASAN 1
#endif
#elif defined(__SANITIZE_ADDRESS__)
#define CORTEX_POOL_HAS_ASAN 1
#endif

#if defined(CORTEX_POOL_HAS_ASAN)
#include <sanitizer/asan_interface.h>
#endif

namespace cortex {

namespace {

// Blocks with stricter alignment than the upstream default are passed
// through untouched, so the free lists only ever hold interchangeable
// blocks keyed by size.
constexpr bool IsPoolableAlignment(std::size_t alignment) noexcept {
    return alignment <= alignof(std::max_align_t);
}

void UnpoisonBlock([[maybe_unused]] void* block, [[maybe_unused]] std::size_t bytes) noexcept {
#if defined(CORTEX_POOL_HAS_ASAN)
    __asan_unpoison_memory_region(block, bytes);
#endif
}

} // namespace

PooledMemoryResource::PooledMemoryResource()
    : PooledMemoryResource(Config {}) {}

PooledMemoryResource::PooledMemoryResource(Config config)
    : config_(std::move(config)) {}

PooledMemoryResource::~PooledMemoryResource() {
    for (auto& [size, blocks] : free_lists_) {
        for (void* block : blocks) {
            config_.upstream->Deallocate(block, size);
        }
    }
}

void* PooledMemoryResource::DoAllocate(std::size_t bytes, std::size_t alignment) {
    if (IsPoolableAlignment(alignment)) {
        auto it = free_lists_.find(bytes);
        if (it != free_lists_.end() && !it->second.empty()) {
            void* block = it->second.back();
            it->second.pop_back();
            cached_bytes_ -= bytes;
            UnpoisonBlock(block, bytes);
            return block;
        }
    }
    return config_.upstream->Allocate(bytes, alignment);
}

void PooledMemoryResource::DoDeallocate(void* p, std::size_t bytes, std::size_t alignment) {
    if (IsPoolableAlignment(alignment) && cached_bytes_ + bytes <= config_.max_cached_bytes) {
        free_lists_[bytes].push_back(p);
        cached_bytes_ += bytes;
        return;
    }
    config_.upstream->Deallocate(p, bytes, alignment);
}

MemoryResourceSharedPtr MakePooledMemoryResource() {
    return std::make_shared<PooledMemoryResource>();
}

MemoryResourceSharedPtr MakePooledMemoryResource(PooledMemoryResource::Config config) {
    return std::make_shared<PooledMemoryResource>(std::move(config));
}

} // namespace cortex
