#include "detail/coroutine_emscripten_impl.hpp"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "cortex/coroutine_suspend_context.hpp"
#include "cortex/errors/resume_on_completed_coroutine_error.hpp"
#include "forced_unwind.hpp"

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

struct FiberSuspendContext final : cortex::CoroutineSuspendContext {
    explicit FiberSuspendContext(CoroutineImpl* impl)
        : impl_(impl) {}

    ~FiberSuspendContext() override = default;

    void Suspend() override {
        emscripten_fiber_t* main_f = &get_main_context().fiber;

        emscripten_fiber_t* old_fiber = running_fiber;
        running_fiber = main_f;

        emscripten_fiber_swap(old_fiber, main_f);

        if (impl_->IsUnwinding()) {
            throw ForcedUnwind {};
        }
    }

private:
    CoroutineImpl* impl_;
};

} // namespace

CoroutineImpl::CoroutineImpl(cortex::CoroutineBody body, std::size_t stack_size, MemoryResourceSharedPtr resource)
    : body_(std::move(body))
    , stack_size_bytes_(stack_size)
    , resource_(std::move(resource)) {
    c_stack_ = resource_->Allocate(stack_size, 16);
    asyncify_stack_ = resource_->Allocate(stack_size, 16);
    std::memset(c_stack_, 0, stack_size);
    std::memset(asyncify_stack_, 0, stack_size);

    emscripten_fiber_init(&fiber_, FiberEntry, this, c_stack_, stack_size, asyncify_stack_, stack_size);
}

CoroutineImpl::~CoroutineImpl() {
    if (!is_done_) {
        is_unwinding_ = true;
        try {
            Resume();
        } catch (...) {
        }
    }
    if (c_stack_) resource_->Deallocate(c_stack_, stack_size_bytes_, 16);
    if (asyncify_stack_) resource_->Deallocate(asyncify_stack_, stack_size_bytes_, 16);
}

void CoroutineImpl::FiberEntry(void* arg) {
    auto* self = static_cast<CoroutineImpl*>(arg);
    assert(self);

    FiberSuspendContext suspend_context(self);

    try {
        self->body_(suspend_context);
    } catch (const ForcedUnwind&) {
        // Unwinding in progress
    } catch (...) {
        self->exception_ptr_ = std::current_exception();
    }

    self->is_done_ = true;

    // Exit fiber back to the main context
    emscripten_fiber_t* main_f = &get_main_context().fiber;
    running_fiber = main_f;
    emscripten_fiber_swap(&self->fiber_, main_f);
}

std::size_t CoroutineImpl::GetStackSize() const noexcept {
    return stack_size_bytes_;
}

bool CoroutineImpl::IsDone() const noexcept {
    return is_done_;
}

bool CoroutineImpl::HasException() const noexcept {
    return static_cast<bool>(exception_ptr_);
}

bool CoroutineImpl::IsUnwinding() const noexcept {
    return is_unwinding_;
}

void CoroutineImpl::Resume() {
    if (IsDone()) {
        throw ResumeOnDoneCoroutineError {"Resume on finished coroutine."};
    }

    // Capture current JS call stack into main fiber context
    emscripten_fiber_init_from_current_context(
        &get_main_context().fiber, get_main_context().asyncify_stack, MainFiberContext::stack_size);

    emscripten_fiber_t* old_fiber = &get_main_context().fiber;
    running_fiber = &fiber_;

    emscripten_fiber_swap(old_fiber, &fiber_);

    // Upon return back to Resume(), the running fiber is main
    running_fiber = old_fiber;

    if (exception_ptr_) {
        std::rethrow_exception(exception_ptr_);
    }
}

} // namespace cortex::detail
