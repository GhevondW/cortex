#include "detail/coroutine_emscripten_impl.hpp"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <emscripten.h>
#include <stdexcept>
#include <utility>

#include "cortex/coroutine_suspend_context.hpp"
#include "cortex/errors/resume_on_completed_coroutine_error.hpp"
#include "cortex/memory_resource.hpp"
#include "forced_unwind.hpp"

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
        std::memset(asyncify_stack, 0, kAsyncifyStackSize);
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
    if (emscripten_has_asyncify() != 1) {
        throw std::runtime_error("Cortex requires ASYNCIFY to be enabled for Emscripten.");
    }

    try {
        c_stack_ = resource_->Allocate(stack_size, kStackAlignment);
        asyncify_stack_ = resource_->Allocate(kAsyncifyStackSize, kStackAlignment);
    } catch (...) {
        if (c_stack_) resource_->Deallocate(c_stack_, stack_size, kStackAlignment);
        throw;
    }
    std::memset(c_stack_, 0, stack_size);
    std::memset(asyncify_stack_, 0, kAsyncifyStackSize);

    emscripten_fiber_init(&fiber_, FiberEntry, this, c_stack_, stack_size, asyncify_stack_, kAsyncifyStackSize);
}

CoroutineImpl::~CoroutineImpl() {
    if (!is_done_) {
        is_unwinding_ = true;
        try {
            Resume();
        } catch (...) {
        }
    }

    if (c_stack_) resource_->Deallocate(c_stack_, stack_size_bytes_, kStackAlignment);
    if (asyncify_stack_) resource_->Deallocate(asyncify_stack_, kAsyncifyStackSize, kStackAlignment);
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

emscripten_fiber_t* CoroutineImpl::GetBackFiber() const noexcept {
    return back_fiber_;
}

void CoroutineImpl::SetBackFiber(emscripten_fiber_t* fiber) noexcept {
    back_fiber_ = fiber;
}

void CoroutineImpl::Resume() {
    if (IsDone()) {
        throw ResumeOnDoneCoroutineError {"Resume on finished coroutine."};
    }

    emscripten_fiber_t* back_f = running_fiber;
    if (!back_f) {
        // Capture current JS call stack into main fiber context if not already in a fiber
        back_f = &GetMainContext().fiber;
        emscripten_fiber_init_from_current_context(back_f, GetMainContext().asyncify_stack, kAsyncifyStackSize);
    }

    back_fiber_ = back_f;
    running_fiber = &fiber_;

    emscripten_fiber_swap(back_f, &fiber_);

    // Upon return back to Resume(), the running fiber is what it was before
    running_fiber = back_f;

    if (exception_ptr_) {
        auto ex = exception_ptr_;
        exception_ptr_ = nullptr;
        std::rethrow_exception(ex);
    }
}

} // namespace cortex::detail
