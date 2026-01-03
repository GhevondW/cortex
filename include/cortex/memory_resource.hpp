#pragma once

#include <cstddef>
#include <memory>

namespace cortex {

class MemoryResource {
public:
    MemoryResource() = default;
    MemoryResource(const MemoryResource&) = default;
    MemoryResource(MemoryResource&&) = default;
    MemoryResource& operator=(const MemoryResource&) = default;
    MemoryResource& operator=(MemoryResource&&) = default;
    virtual ~MemoryResource() = default;

    void* Allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t)) {
        return DoAllocate(bytes, alignment);
    }

    void Deallocate(void* p, std::size_t bytes, std::size_t alignment = alignof(std::max_align_t)) {
        DoDeallocate(p, bytes, alignment);
    }

private:
    virtual void* DoAllocate(std::size_t bytes, std::size_t alignment) = 0;
    virtual void DoDeallocate(void* p, std::size_t bytes, std::size_t alignment) = 0;
};

using MemoryResourceSharedPtr = std::shared_ptr<MemoryResource>;

MemoryResourceSharedPtr GetDefaultMemoryResource();

} // namespace cortex
