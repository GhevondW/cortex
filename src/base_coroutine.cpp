#include <cortex/base_coroutine.hpp>
#include <cortex/coroutine_suspend_context.hpp>

namespace cortex {

BaseCoroutine::BaseCoroutine(const std::size_t stack_size_bytes, MemoryResourceSharedPtr resource, bool reusable)
    : coroutine_(Coroutine::MakeInternal(
          [this](CoroutineSuspendContext& self) {
              this->Continuation(self);
          },
          stack_size_bytes,
          std::move(resource),
          reusable)) {}

BaseCoroutine::~BaseCoroutine() = default;

void BaseCoroutine::ResetCoroutineForReuse() {
    coroutine_.RebindForReuseInternal();
}

} // namespace cortex
