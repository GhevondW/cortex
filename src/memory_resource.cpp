#include <cortex/memory_resource.hpp>
#include <cstdlib>
#include <new>

namespace cortex {

namespace {

class DefaultMemoryResource final : public MemoryResource {
public:
    void* DoAllocate(std::size_t bytes, std::size_t alignment) override {
#if defined(_MSC_VER) || defined(__MINGW32__)
        void* ptr = _aligned_malloc(bytes, alignment);
        if (!ptr) throw std::bad_alloc();
        return ptr;
#else
        void* ptr = nullptr;
        if (posix_memalign(&ptr, alignment < sizeof(void*) ? sizeof(void*) : alignment, bytes) != 0) {
            throw std::bad_alloc();
        }
        return ptr;
#endif
    }

    void DoDeallocate(void* p, std::size_t /*bytes*/, std::size_t /*alignment*/) override {
#if defined(_MSC_VER) || defined(__MINGW32__)
        _aligned_free(p);
#else
        std::free(p);
#endif
    }
};

} // namespace

MemoryResourceSharedPtr GetDefaultMemoryResource() {
    static auto resource = std::make_shared<DefaultMemoryResource>();
    return resource;
}

} // namespace cortex
