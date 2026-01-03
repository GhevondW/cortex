#include <cortex/base_coroutine.hpp>
#include <cortex/coroutine_suspend_context.hpp>

namespace cortex {

BaseCoroutine::BaseCoroutine(const std::size_t stack_size_bytes, MemoryResourceSharedPtr resource)
    : coroutine_(Coroutine::Make(
          [this](CoroutineSuspendContext& self) {
              this->Continuation(self);
          },
          stack_size_bytes,
          std::move(resource))) {}

BaseCoroutine::~BaseCoroutine() = default;

} // namespace cortex
