#include <cortex/memory_resource.hpp>
#include <cstdlib>
#include <memory_resource>
#include <new>

namespace cortex {

namespace {

class DefaultMemoryResource final : public MemoryResource {
public:
    DefaultMemoryResource()
        : memory_resource_(std::pmr::get_default_resource()) {}
    ~DefaultMemoryResource() override = default;

    void* DoAllocate(std::size_t bytes, std::size_t alignment) override {
        return memory_resource_->allocate(bytes, alignment);
    }

    void DoDeallocate(void* p, std::size_t bytes, std::size_t alignment) override {
        memory_resource_->deallocate(p, bytes, alignment);
    }

private:
    std::pmr::memory_resource* memory_resource_;
};

} // namespace

MemoryResourceSharedPtr GetDefaultMemoryResource() {
    static auto resource = std::make_shared<DefaultMemoryResource>();
    return resource;
}

} // namespace cortex
