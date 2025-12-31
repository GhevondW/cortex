#include "cortex/detail/coroutine_native.hpp"

#include <cassert>
#include <exception>
#include <utility>

#include "cortex/errors/resume_on_completed_coroutine_error.hpp"

namespace cortex::detail {

namespace {

struct FiberSuspendContext final : cortex::CoroutineSuspendContext {
    explicit FiberSuspendContext(boost::context::fiber& sink)
        : _sink(sink) {}

    ~FiberSuspendContext() override = default;

    void Suspend() override {
        _sink = std::move(_sink).resume();
    }

private:
    boost::context::fiber& _sink;
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
    , exception_ptr_(nullptr)
    , fiber_([this, body = std::move(body)](boost::context::fiber&& sink) mutable {
        assert(static_cast<bool>(body));
        FiberSuspendContext suspend_context(sink);

        try {
            body(suspend_context);
        } catch (...) {
            exception_ptr_ = std::current_exception();
        }

        is_done_ = true;

        return std::move(sink);
    }) {
    assert(stack_size_bytes_ > 0);
}

std::size_t Coroutine::GetStackSize() const noexcept {
    return stack_size_bytes_;
}

bool Coroutine::IsDone() const noexcept {
    return is_done_;
}

[[nodiscard]] bool Coroutine::HasException() const noexcept {
    return static_cast<bool>(exception_ptr_);
}

// @throws ResumeOnDoneCoroutineError
void Coroutine::Resume() {
    if (IsDone()) {
        throw ResumeOnDoneCoroutineError {"Resume on finished coroutine."};
    }

    fiber_ = std::move(fiber_).resume();

    if (exception_ptr_) {
        std::rethrow_exception(exception_ptr_);
    }
}

} // namespace cortex::detail
