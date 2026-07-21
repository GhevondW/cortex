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

/**
 * @class MemoryResourceAllocator
 * @brief Standard-allocator adapter over a MemoryResource.
 *
 * Lets standard containers and std::allocate_shared draw memory from a
 * MemoryResource. Keeps the resource alive via shared ownership, so
 * allocations may safely outlive the code that created the allocator.
 */
template <typename T>
class MemoryResourceAllocator {
public:
    using value_type = T;

    explicit MemoryResourceAllocator(MemoryResourceSharedPtr resource) noexcept
        : resource_(std::move(resource)) {}

    template <typename U>
    MemoryResourceAllocator(const MemoryResourceAllocator<U>& other) noexcept
        : resource_(other.GetResource()) {}

    T* allocate(std::size_t n) {
        return static_cast<T*>(resource_->Allocate(n * sizeof(T), alignof(T)));
    }

    void deallocate(T* p, std::size_t n) noexcept {
        resource_->Deallocate(p, n * sizeof(T), alignof(T));
    }

    [[nodiscard]] const MemoryResourceSharedPtr& GetResource() const noexcept {
        return resource_;
    }

    template <typename U>
    bool operator==(const MemoryResourceAllocator<U>& other) const noexcept {
        return resource_ == other.GetResource();
    }

private:
    MemoryResourceSharedPtr resource_;
};

} // namespace cortex
