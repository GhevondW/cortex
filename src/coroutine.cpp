#include <cortex/config.hpp>
#include <cortex/coroutine.hpp>

#include <cassert>
#include <new>
#include <stdexcept>
#include <utility>

#if defined(CORTEX_EMSCRIPTEN)
#include "detail/coroutine_emscripten_impl.hpp"
#else
#include "detail/coroutine_native_impl.hpp"
#endif

namespace cortex {

void Coroutine::ImplDeleter::operator()(detail::CoroutineImpl* impl) const {
    if (impl) {
        impl->~CoroutineImpl();
        assert(resource);
        resource->Deallocate(impl, sizeof(detail::CoroutineImpl), alignof(detail::CoroutineImpl));
    }
}

Coroutine::Builder::Builder()
    : stack_size_bytes_(262144)
    , memory_resource_(GetDefaultMemoryResource()) {}

Coroutine Coroutine::Builder::Build(CoroutineBody body) && {
    return Coroutine::Make(std::move(body), stack_size_bytes_, std::move(memory_resource_));
}

Coroutine::Builder Coroutine::Builder::SetStackSizeInBytes(std::size_t stack_size_bytes) && noexcept {
    stack_size_bytes_ = stack_size_bytes;
    return std::move(*this);
}

Coroutine::Builder Coroutine::Builder::SetMemoryResource(MemoryResourceSharedPtr resource) && noexcept {
    memory_resource_ = std::move(resource);
    return std::move(*this);
}

Coroutine Coroutine::Make(CoroutineBody body, std::size_t stack_size_bytes, MemoryResourceSharedPtr resource) {
    if (!static_cast<bool>(body)) {
        throw std::invalid_argument("coroutine body is null.");
    }

    if (stack_size_bytes == 0) {
        throw std::invalid_argument("stack_size_bytes is 0.");
    }

    if (!resource) {
        throw std::invalid_argument("memory_resource is null.");
    }

    void* ptr = resource->Allocate(sizeof(detail::CoroutineImpl), alignof(detail::CoroutineImpl));
    try {
        auto* impl = new (ptr) detail::CoroutineImpl(std::move(body), stack_size_bytes, resource);
        return Coroutine(std::unique_ptr<detail::CoroutineImpl, ImplDeleter>(impl, ImplDeleter {std::move(resource)}));
    } catch (...) {
        resource->Deallocate(ptr, sizeof(detail::CoroutineImpl), alignof(detail::CoroutineImpl));
        throw;
    }
}

Coroutine::Coroutine(std::unique_ptr<detail::CoroutineImpl, ImplDeleter> impl)
    : impl_(std::move(impl)) {}

Coroutine::Coroutine(Coroutine&&) noexcept = default;
Coroutine& Coroutine::operator=(Coroutine&&) noexcept = default;

Coroutine::~Coroutine() = default;

std::size_t Coroutine::GetStackSize() const noexcept {
    assert(impl_);
    return impl_->GetStackSize();
}

bool Coroutine::IsDone() const noexcept {
    assert(impl_);
    return impl_->IsDone();
}

bool Coroutine::HasException() const noexcept {
    assert(impl_);
    return impl_->HasException();
}

void Coroutine::Resume() {
    assert(impl_);
    impl_->Resume();
}

} // namespace cortex
