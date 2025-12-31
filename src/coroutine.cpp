#include <cortex/config.hpp>
#include <cortex/coroutine.hpp>

#include <cassert>
#include <stdexcept>
#include <utility>

#if defined(CORTEX_EMSCRIPTEN)
#include "detail/coroutine_emscripten_impl.hpp"
#else
#include "detail/coroutine_native_impl.hpp"
#endif

namespace cortex {

Coroutine Coroutine::Make(CoroutineBody body, std::size_t stack_size_bytes) {
    if (!static_cast<bool>(body)) {
        throw std::invalid_argument("coroutine body is null.");
    }

    if (stack_size_bytes == 0) {
        throw std::invalid_argument("stack_size_bytes is 0.");
    }

#if defined(CORTEX_EMSCRIPTEN)
    return Coroutine(std::make_unique<detail::CoroutineImpl>(std::move(body), stack_size_bytes));
#else
    return Coroutine(std::make_unique<detail::CoroutineImpl>(std::move(body), stack_size_bytes));
#endif
}

Coroutine::Coroutine(std::unique_ptr<detail::CoroutineImpl> impl)
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
