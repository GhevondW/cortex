#include "detail/coroutine_emscripten_impl.hpp"

#include <cassert>
#include <cstdlib>
#include <emscripten.h>
#include <stdexcept>
#include <utility>

#include "cortex/coroutine_suspend_context.hpp"
#include "cortex/errors/resume_on_completed_coroutine_error.hpp"
#include "cortex/memory_resource.hpp"
#include <cortex/detail/forced_unwind.hpp>

namespace cortex::detail {

namespace {

static constexpr std::size_t kAsyncifyStackSize = 16384;
static constexpr std::size_t kStackAlignment = 16;

// Track the currently executing fiber
thread_local emscripten_fiber_t* running_fiber = nullptr;

// Persistent state for the "main" fiber (the one calling Resume from JS)
struct MainFiberContext {
    emscripten_fiber_t fiber;
    void* asyncify_stack {nullptr};
    cortex::MemoryResourceSharedPtr resource;

    MainFiberContext()
        : resource(cortex::GetDefaultMemoryResource()) {
        asyncify_stack = resource->Allocate(kAsyncifyStackSize, kStackAlignment);
    }
    ~MainFiberContext() {
        if (asyncify_stack) {
            resource->Deallocate(asyncify_stack, kAsyncifyStackSize, kStackAlignment);
        }
    }
};

MainFiberContext& GetMainContext() {
    thread_local MainFiberContext instance;
    return instance;
}

struct FiberSuspendContext final : cortex::CoroutineSuspendContext {
    explicit FiberSuspendContext(CoroutineImpl* impl)
        : impl_(impl) {}

    ~FiberSuspendContext() override = default;

    void Suspend() override {
        emscripten_fiber_t* back_f = impl_->GetBackFiber();
        emscripten_fiber_t* current_f = running_fiber;

        running_fiber = back_f;

        emscripten_fiber_swap(current_f, back_f);

        if (impl_->IsUnwinding() || impl_->ShouldAbortBody()) {
            throw ForcedUnwind {};
        }
    }

private:
    CoroutineImpl* impl_;
};

} // namespace

CoroutineImpl::CoroutineImpl(cortex::CoroutineBody body,
                             std::size_t stack_size,
                             const MemoryResourceSharedPtr& resource,
                             bool reusable)
    : body_(std::move(body))
    , reusable_(reusable)
    , stack_size_bytes_(stack_size)
    , resource_(resource) {
    if (emscripten_has_asyncify() != 1) {
        throw std::runtime_error("Cortex requires ASYNCIFY to be enabled for Emscripten.");
    }

    // Neither stack needs zero-initialization: emscripten_fiber_init writes
    // the asyncify bookkeeping itself, and the C stack contents are written
    // before use. Zeroing them cost a 256KB+16KB memset per coroutine.
    try {
        c_stack_ = resource_->Allocate(stack_size, kStackAlignment);
        asyncify_stack_ = resource_->Allocate(kAsyncifyStackSize, kStackAlignment);
    } catch (...) {
        if (c_stack_) resource_->Deallocate(c_stack_, stack_size, kStackAlignment);
        throw;
    }

    emscripten_fiber_init(&fiber_, FiberEntry, this, c_stack_, stack_size, asyncify_stack_, kAsyncifyStackSize);
}

CoroutineImpl::~CoroutineImpl() {
    // A started context (mid-body or parked) must be unwound so the
    // trampoline exits. A never-started context must NOT be swapped in —
    // that would execute the body during destruction; the fiber never ran,
    // so freeing the stacks below is all the cleanup it needs. A finished
    // one-shot is already dead.
    const bool context_alive = started_ && (reusable_ || !is_done_);
    if (context_alive) {
        is_unwinding_ = true;
        SwapIn();
    }

    if (c_stack_) resource_->Deallocate(c_stack_, stack_size_bytes_, kStackAlignment);
    if (asyncify_stack_) resource_->Deallocate(asyncify_stack_, kAsyncifyStackSize, kStackAlignment);
}

void CoroutineImpl::FiberEntry(void* arg) {
    auto* self = static_cast<CoroutineImpl*>(arg);
    assert(self);
    self->started_ = true;

    FiberSuspendContext suspend_context(self);

    for (;;) {
        self->body_started_ = true;
        try {
            self->body_(suspend_context);
        } catch (const ForcedUnwind&) {
            // Unwinding in progress or the body is being aborted
        } catch (...) {
            self->exception_ptr_ = std::current_exception();
        }

        self->is_done_ = true;

        if (!self->reusable_) {
            // Release the body's captures at completion, matching the
            // pre-trampoline body lifetime.
            self->body_ = cortex::CoroutineBody {};
            break;
        }
        if (self->is_unwinding_) {
            break;
        }

        // Park: swap back and wait for Rebind() + Resume(), or teardown.
        emscripten_fiber_t* park_back_f = self->back_fiber_;
        running_fiber = park_back_f;
        emscripten_fiber_swap(&self->fiber_, park_back_f);

        if (self->is_unwinding_) {
            break;
        }
    }

    // Exit fiber back to the context that resumed us
    emscripten_fiber_t* back_f = self->back_fiber_;
    running_fiber = back_f;
    emscripten_fiber_swap(&self->fiber_, back_f);
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

bool CoroutineImpl::ShouldAbortBody() const noexcept {
    return abort_body_;
}

emscripten_fiber_t* CoroutineImpl::GetBackFiber() const noexcept {
    return back_fiber_;
}

void CoroutineImpl::SetBackFiber(emscripten_fiber_t* fiber) noexcept {
    back_fiber_ = fiber;
}

void CoroutineImpl::SwapIn() {
    emscripten_fiber_t* back_f = running_fiber;
    if (!back_f) {
        // Capture current JS call stack into main fiber context if not already in a fiber
        back_f = &GetMainContext().fiber;
        emscripten_fiber_init_from_current_context(back_f, GetMainContext().asyncify_stack, kAsyncifyStackSize);
    }

    back_fiber_ = back_f;
    running_fiber = &fiber_;

    emscripten_fiber_swap(back_f, &fiber_);

    // Upon return back here, the running fiber is what it was before
    running_fiber = back_f;
}

void CoroutineImpl::Resume() {
    if (IsDone()) {
        throw ResumeOnDoneCoroutineError {"Resume on finished coroutine."};
    }

    SwapIn();

    if (exception_ptr_) {
        auto ex = exception_ptr_;
        exception_ptr_ = nullptr;
        std::rethrow_exception(ex);
    }
}

void CoroutineImpl::Rebind(cortex::CoroutineBody body) {
    assert(reusable_);
    if (body_started_ && !is_done_) {
        throw std::logic_error("Rebind on a coroutine whose body has not finished.");
    }
    body_ = std::move(body);
    // Discard any leftover exception from an aborted body: its outcome is
    // dropped by definition and must not leak into the next run.
    exception_ptr_ = nullptr;
    body_started_ = false;
    is_done_ = false;
}

void CoroutineImpl::Rebind() {
    assert(reusable_);
    if (body_started_ && !is_done_) {
        throw std::logic_error("Rebind on a coroutine whose body has not finished.");
    }
    exception_ptr_ = nullptr;
    body_started_ = false;
    is_done_ = false;
}

void CoroutineImpl::AbortBody() {
    assert(reusable_);
    if (is_done_ || !body_started_) {
        return;
    }
    abort_body_ = true;
    SwapIn();
    abort_body_ = false;
}

} // namespace cortex::detail
