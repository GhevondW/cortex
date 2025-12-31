#include "cortex/detail/coroutine_emscripten.hpp"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <emscripten/fiber.h>
#include <exception>
#include <utility>

#include "cortex/detail/forced_unwind.hpp"
#include "cortex/errors/resume_on_completed_coroutine_error.hpp"

namespace cortex::detail {

namespace {

// Track the currently executing fiber
thread_local emscripten_fiber_t* running_fiber = nullptr;

// Persistent state for the "main" fiber (the one calling Resume from JS)
struct MainFiberContext {
    emscripten_fiber_t fiber;
    void* asyncify_stack;
    static constexpr size_t stack_size = 262144;

    MainFiberContext() {
        asyncify_stack = std::aligned_alloc(16, stack_size);
        std::memset(asyncify_stack, 0, stack_size);
    }
    ~MainFiberContext() {
        std::free(asyncify_stack);
    }
};

MainFiberContext& get_main_context() {
    thread_local MainFiberContext instance;
    return instance;
}

} // namespace

struct Coroutine::Impl {
    emscripten_fiber_t fiber;
    CoroutineBody body;
    bool is_done {false};
    bool is_unwinding {false};
    std::size_t stack_size_bytes;
    std::exception_ptr exception_ptr;
    void* c_stack {nullptr};
    void* asyncify_stack {nullptr};

    Impl(CoroutineBody b, std::size_t stack_size)
        : body(std::move(b))
        , stack_size_bytes(stack_size) {
        c_stack = std::aligned_alloc(16, stack_size);
        asyncify_stack = std::aligned_alloc(16, stack_size);
        std::memset(c_stack, 0, stack_size);
        std::memset(asyncify_stack, 0, stack_size);

        emscripten_fiber_init(&fiber, FiberEntry, this, c_stack, stack_size, asyncify_stack, stack_size);
    }

    ~Impl() {
        if (c_stack) std::free(c_stack);
        if (asyncify_stack) std::free(asyncify_stack);
    }

    static void FiberEntry(void* arg);
};

struct FiberSuspendContext final : cortex::CoroutineSuspendContext {
    explicit FiberSuspendContext(Coroutine::Impl* impl)
        : impl_(impl) {}

    ~FiberSuspendContext() override = default;

    void Suspend() override {
        emscripten_fiber_t* main_f = &get_main_context().fiber;

        emscripten_fiber_t* old_fiber = running_fiber;
        running_fiber = main_f;

        emscripten_fiber_swap(old_fiber, main_f);

        if (impl_->is_unwinding) {
            throw ForcedUnwind {};
        }
    }

private:
    Coroutine::Impl* impl_;
};

void Coroutine::Impl::FiberEntry(void* arg) {
    auto* self = static_cast<Coroutine::Impl*>(arg);
    assert(self);

    FiberSuspendContext suspend_context(self);

    try {
        self->body(suspend_context);
    } catch (const ForcedUnwind&) {
        // Unwinding in progress
    } catch (...) {
        self->exception_ptr = std::current_exception();
    }

    self->is_done = true;

    // Exit fiber back to the main context
    emscripten_fiber_t* main_f = &get_main_context().fiber;
    running_fiber = main_f;
    emscripten_fiber_swap(&self->fiber, main_f);
}

Coroutine Coroutine::Make(cortex::CoroutineBody body, std::size_t stack_size_bytes) {
    if (!static_cast<bool>(body)) {
        throw std::invalid_argument("coroutine body is null.");
    }

    if (stack_size_bytes == 0) {
        throw std::invalid_argument("stack_size_bytes is 0.");
    }

    auto impl = std::make_unique<Impl>(std::move(body), stack_size_bytes);
    return Coroutine(std::move(impl));
}

Coroutine::Coroutine(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

Coroutine::Coroutine(Coroutine&&) noexcept = default;
Coroutine& Coroutine::operator=(Coroutine&&) noexcept = default;

Coroutine::~Coroutine() {
    if (impl_ && !impl_->is_done) {
        impl_->is_unwinding = true;
        try {
            Resume();
        } catch (...) {
        }
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

bool Coroutine::HasException() const noexcept {
    assert(impl_);
    return static_cast<bool>(impl_->exception_ptr);
}

void Coroutine::Resume() {
    assert(impl_);
    if (IsDone()) {
        throw ResumeOnDoneCoroutineError {"Resume on finished coroutine."};
    }

    // Capture current JS call stack into main fiber context
    emscripten_fiber_init_from_current_context(
        &get_main_context().fiber, get_main_context().asyncify_stack, MainFiberContext::stack_size);

    emscripten_fiber_t* old_fiber = &get_main_context().fiber;
    running_fiber = &impl_->fiber;

    emscripten_fiber_swap(old_fiber, &impl_->fiber);

    // Upon return back to Resume(), the running fiber is main
    running_fiber = old_fiber;

    if (impl_->exception_ptr) {
        std::rethrow_exception(impl_->exception_ptr);
    }
}

} // namespace cortex::detail
