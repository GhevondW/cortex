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

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define CORTEX_ASAN_ENABLED 1
#endif
#endif
#if !defined(CORTEX_ASAN_ENABLED) && defined(__SANITIZE_ADDRESS__)
#define CORTEX_ASAN_ENABLED 1
#endif

#ifdef CORTEX_ASAN_ENABLED
#include <sanitizer/common_interface_defs.h>
#endif

namespace cortex::detail {

namespace {

// Suspending a coroutine saves the WebAssembly locals of every frame on its
// stack into this buffer, so its size bounds how deep a coroutine may be when
// it suspends. Measured: a small recursive function needs 16-24 bytes per
// frame (2000 frames overflow 32 KB and fit in 48 KB), so the default 64 KB
// allows a few thousand frames; with -fsanitize=address frames need 2-3x as
// much. Overflowing it aborts with "RuntimeError: unreachable". Configure with
// the CORTEX_WASM_ASYNCIFY_STACK_SIZE CMake cache variable.
#ifndef CORTEX_WASM_ASYNCIFY_STACK_SIZE
#define CORTEX_WASM_ASYNCIFY_STACK_SIZE 65536
#endif
static constexpr std::size_t kAsyncifyStackSize = CORTEX_WASM_ASYNCIFY_STACK_SIZE;
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

        impl_->OnSwitchingOut(/*finished=*/false);
        emscripten_fiber_swap(current_f, back_f);
        impl_->OnSwitchedIn();

        if (impl_->IsUnwinding()) {
            throw ForcedUnwind {};
        }
    }

private:
    CoroutineImpl* impl_;
};

} // namespace

CoroutineImpl::CoroutineImpl(cortex::CoroutineBody body,
                             std::size_t stack_size,
                             const MemoryResourceSharedPtr& resource)
    : body_(std::move(body))
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
    self->OnSwitchedIn();

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
    self->OnSwitchingOut(/*finished=*/true);
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
    switched_in_ = false;

#ifdef CORTEX_ASAN_ENABLED
    void* fake_stack = nullptr;
    __sanitizer_start_switch_fiber(&fake_stack, c_stack_, stack_size_bytes_);
#endif
    emscripten_fiber_swap(back_f, &fiber_);
#ifdef CORTEX_ASAN_ENABLED
    __sanitizer_finish_switch_fiber(fake_stack, nullptr, nullptr);
#endif

    // Upon return back to Resume(), the running fiber is what it was before
    running_fiber = back_f;

    if (!switched_in_) {
        // emscripten_fiber_swap() does nothing once the runtime has exited or
        // aborted. Without this check the caller would carry on as if the
        // coroutine had run and suspended.
        throw std::runtime_error("cortex: cannot switch to a coroutine: the Emscripten runtime has exited or aborted. "
                                 "Keep it alive with -sEXIT_RUNTIME=0 (the default, except with -fsanitize=address).");
    }

    if (exception_ptr_) {
        auto ex = exception_ptr_;
        exception_ptr_ = nullptr;
        std::rethrow_exception(ex);
    }
}

void CoroutineImpl::OnSwitchedIn() noexcept {
    switched_in_ = true;
#ifdef CORTEX_ASAN_ENABLED
    __sanitizer_finish_switch_fiber(asan_fake_stack_, &back_stack_bottom_, &back_stack_size_);
#endif
}

void CoroutineImpl::OnSwitchingOut([[maybe_unused]] bool finished) noexcept {
#ifdef CORTEX_ASAN_ENABLED
    // A null save slot tells ASan this stack is done, so it frees its state.
    __sanitizer_start_switch_fiber(finished ? nullptr : &asan_fake_stack_, back_stack_bottom_, back_stack_size_);
#endif
}

} // namespace cortex::detail
