#include "cortex/detail/coroutine_native.hpp"

#include <cassert>
#include <exception>
#include <utility>

#include <boost/context/fiber.hpp>

#include "cortex/detail/forced_unwind.hpp"
#include "cortex/errors/resume_on_completed_coroutine_error.hpp"

namespace cortex::detail {

struct Coroutine::Impl {
    bool is_done {false};
    bool is_unwinding {false};
    std::size_t stack_size_bytes;
    std::exception_ptr exception_ptr {nullptr};
    boost::context::fiber fiber;

    Impl(cortex::CoroutineBody body, std::size_t stack_size);
};

namespace {

struct FiberSuspendContext final : cortex::CoroutineSuspendContext {
    explicit FiberSuspendContext(boost::context::fiber& sink, Coroutine::Impl* impl)
        : _sink(sink)
        , _impl(impl) {}

    ~FiberSuspendContext() override = default;

    void Suspend() override {
        _sink = std::move(_sink).resume();
        // If the coroutine is being destroyed, we throw ForcedUnwind to trigger stack unwinding.
        // While boost::context::fiber has internal unwinding support, it relies on throwing
        // a special internal exception. If the user catches that exception (e.g. via catch (...))
        // and doesn't rethrow it, or if we catch it in our wrapper and try to return a
        // context normally, Boost will crash with an assertion failure (nullptr != t.fctx).
        // By using our own ForcedUnwind and an explicit is_unwinding flag, we provide
        // a unified, cross-platform mechanism that safely exits the coroutine body.

        // NOTE: To ensure proper resource cleanup via ForcedUnwind, users should
        // avoid swallowing all exceptions with catch (...). It is recommended
        // to only catch specific exceptions (like const std::exception&) or
        // always rethrow when using catch (...). Swallowing ForcedUnwind
        // will prevent complete stack unwinding and is considered undefined behavior.

        // In future implementations, we may consider dropping support for catch(...) and
        // expecting the coroutine body to only catch exceptions derived from std::exception.
        // Catching other types of exceptions would then be considered undefined behavior.
        if (_impl->is_unwinding) {
            throw ForcedUnwind {};
        }
    }

private:
    boost::context::fiber& _sink;
    Coroutine::Impl* _impl;
};

} // namespace

Coroutine::Impl::Impl(cortex::CoroutineBody body, std::size_t stack_size)
    : stack_size_bytes(stack_size)
    , fiber([this, body = std::move(body)](boost::context::fiber&& sink) mutable {
        assert(static_cast<bool>(body));
        FiberSuspendContext suspend_context(sink, this);

        try {
            body(suspend_context);
        } catch (const ForcedUnwind&) {
            // Unwinding in progress (triggered by destructor), just exit the coroutine.
        } catch (...) {
            // Capture any other exceptions to be rethrown in Resume().
            assert(!static_cast<bool>(exception_ptr));
            exception_ptr = std::current_exception();
        }

        is_done = true;

        return std::move(sink);
    }) {
    assert(stack_size_bytes > 0);
}

Coroutine Coroutine::Make(cortex::CoroutineBody body, std::size_t stack_size_bytes) {
    if (!static_cast<bool>(body)) {
        throw std::invalid_argument("coroutine body is null.");
    }

    if (stack_size_bytes == 0) {
        throw std::invalid_argument("stack_size_bytes is 0.");
    }

    return Coroutine(std::make_unique<Impl>(std::move(body), stack_size_bytes));
}

Coroutine::Coroutine(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

Coroutine::Coroutine(Coroutine&&) noexcept = default;
Coroutine& Coroutine::operator=(Coroutine&&) noexcept = default;

Coroutine::~Coroutine() {
    if (impl_ && !impl_->is_done && impl_->fiber) {
        impl_->is_unwinding = true;
        // Resuming the fiber will cause Suspend() to throw ForcedUnwind
        impl_->fiber = std::move(impl_->fiber).resume();
    }
}

std::size_t Coroutine::GetStackSize() const noexcept {
    assert(impl_);
    return impl_->stack_size_bytes;
}

bool Coroutine::IsDone() const noexcept {
    assert(impl_);
    return impl_->is_done;
}

[[nodiscard]] bool Coroutine::HasException() const noexcept {
    assert(impl_);
    return static_cast<bool>(impl_->exception_ptr);
}

// @throws ResumeOnDoneCoroutineError
void Coroutine::Resume() {
    assert(impl_);
    if (IsDone()) {
        throw ResumeOnDoneCoroutineError {"Resume on finished coroutine."};
    }

    impl_->fiber = std::move(impl_->fiber).resume();

    if (impl_->exception_ptr) {
        std::rethrow_exception(impl_->exception_ptr);
    }
}

} // namespace cortex::detail
