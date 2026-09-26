#include <cortex/guarded_stack_resource.hpp>

#include <cstdint>
#include <new>
#include <utility>

#if !defined(__EMSCRIPTEN__) && (defined(__unix__) || defined(__APPLE__))
#define CORTEX_HAS_GUARD_PAGES 1
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace cortex {

namespace {

#if defined(CORTEX_HAS_GUARD_PAGES)

std::size_t PageSize() noexcept {
    static const std::size_t page_size = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    return page_size;
}

std::size_t RoundUpToPages(std::size_t bytes) noexcept {
    const std::size_t page = PageSize();
    return (bytes + page - 1) / page * page;
}

class GuardedStackResource final : public MemoryResource {
public:
    explicit GuardedStackResource(GuardedStackResourceConfig config)
        : config_(std::move(config)) {}

private:
    [[nodiscard]] bool IsGuarded(std::size_t bytes, std::size_t alignment) const noexcept {
        return bytes >= config_.min_guarded_bytes && alignment <= PageSize();
    }

    void* DoAllocate(std::size_t bytes, std::size_t alignment) override {
        if (!IsGuarded(bytes, alignment)) {
            return config_.upstream->Allocate(bytes, alignment);
        }

        const std::size_t page = PageSize();
        const std::size_t total = RoundUpToPages(bytes) + page;
        void* base = mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (base == MAP_FAILED) {
            throw std::bad_alloc();
        }
        // The lowest page is the guard: stacks grow down towards it.
        if (mprotect(base, page, PROT_NONE) != 0) {
            munmap(base, total);
            throw std::bad_alloc();
        }
        return static_cast<char*>(base) + page;
    }

    void DoDeallocate(void* p, std::size_t bytes, std::size_t alignment) override {
        if (!IsGuarded(bytes, alignment)) {
            config_.upstream->Deallocate(p, bytes, alignment);
            return;
        }
        const std::size_t page = PageSize();
        munmap(static_cast<char*>(p) - page, RoundUpToPages(bytes) + page);
    }

    GuardedStackResourceConfig config_;
};

#endif

} // namespace

bool HasGuardPageSupport() noexcept {
#if defined(CORTEX_HAS_GUARD_PAGES)
    return true;
#else
    return false;
#endif
}

MemoryResourceSharedPtr MakeGuardedStackResource(GuardedStackResourceConfig config) {
#if defined(CORTEX_HAS_GUARD_PAGES)
    return std::make_shared<GuardedStackResource>(std::move(config));
#else
    return std::move(config.upstream);
#endif
}

} // namespace cortex
