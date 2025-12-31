#include "cortex/detail/coroutine_emscripten.hpp"

#include <cassert>
#include <exception>
#include <utility>

#include "cortex/errors/resume_on_completed_coroutine_error.hpp"

namespace cortex::detail {

namespace {

thread_local emscripten_fiber_t* current_caller_fiber = nullptr;

struct FiberSuspendContext final : cortex::CoroutineSuspendContext {
    explicit FiberSuspendContext(emscripten_fiber_t* fiber)
        : fiber_(fiber) {}

    ~FiberSuspendContext() override = default;

    void Suspend() override {
        assert(current_caller_fiber != nullptr);
        emscripten_fiber_swap(fiber_, current_caller_fiber);
    }

private:
    emscripten_fiber_t* fiber_;
};

} // namespace

Coroutine Coroutine::Make(cortex::CoroutineBody body, std::size_t stack_size_bytes) {
    if (!static_cast<bool>(body)) {
        throw std::invalid_argument("coroutine body is null.");
    }

    if (stack_size_bytes == 0) {
        throw std::invalid_argument("stack_size_bytes is 0.");
    }

    return Coroutine(std::move(body), stack_size_bytes);
}

Coroutine::Coroutine(cortex::CoroutineBody body, std::size_t stack_size_bytes)
    : is_done_(false)
    , stack_size_bytes_(stack_size_bytes)
    , body_(std::move(body))
    , c_stack_(stack_size_bytes)
    , asyncify_stack_(stack_size_bytes) {
    emscripten_fiber_init(
        &fiber_, FiberEntry, this, c_stack_.data(), c_stack_.size(), asyncify_stack_.data(), asyncify_stack_.size());
}

std::size_t Coroutine::GetStackSize() const noexcept {
    return stack_size_bytes_;
}

bool Coroutine::IsDone() const noexcept {
    return is_done_;
}

bool Coroutine::HasException() const noexcept {
    return static_cast<bool>(exception_ptr_);
}

void Coroutine::Resume() {
    if (IsDone()) {
        throw ResumeOnDoneCoroutineError {"Resume on finished coroutine."};
    }

    emscripten_fiber_t caller_fiber;
    std::vector<char> caller_asyncify_stack(stack_size_bytes_);
    emscripten_fiber_init_from_current_context(
        &caller_fiber, caller_asyncify_stack.data(), caller_asyncify_stack.size());

    emscripten_fiber_t* prev_caller = current_caller_fiber;
    current_caller_fiber = &caller_fiber;

    emscripten_fiber_swap(&caller_fiber, &fiber_);

    current_caller_fiber = prev_caller;

    if (exception_ptr_) {
        std::rethrow_exception(exception_ptr_);
    }
}

void Coroutine::FiberEntry(void* arg) {
    auto* self = static_cast<Coroutine*>(arg);
    FiberSuspendContext suspend_context(&self->fiber_);

    try {
        self->body_(suspend_context);
    } catch (...) {
        self->exception_ptr_ = std::current_exception();
    }

    self->is_done_ = true;

    assert(current_caller_fiber != nullptr);
    emscripten_fiber_swap(&self->fiber_, current_caller_fiber);
}

} // namespace cortex::detail
