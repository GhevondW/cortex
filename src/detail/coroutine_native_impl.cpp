#include "detail/coroutine_native_impl.hpp"

#include <cassert>
#include <stdexcept>
#include <utility>

#include "cortex/coroutine_suspend_context.hpp"
#include "cortex/errors/resume_on_completed_coroutine_error.hpp"
#include <cortex/detail/forced_unwind.hpp>

namespace cortex::detail {

namespace {

struct FiberSuspendContext final : cortex::CoroutineSuspendContext {
    explicit FiberSuspendContext(boost::context::fiber& sink, CoroutineImpl* impl)
        : _sink(sink)
        , _impl(impl) {}

    ~FiberSuspendContext() override = default;

    void Suspend() override {
        _sink = std::move(_sink).resume();
        if (_impl->IsUnwinding() || _impl->ShouldAbortBody()) {
            throw ForcedUnwind {};
        }
    }

private:
    boost::context::fiber& _sink;
    CoroutineImpl* _impl;
};

struct MemoryResourceStackAllocator {
    MemoryResourceSharedPtr resource;
    std::size_t size;

    boost::context::stack_context allocate() {
        void* vp = resource->Allocate(size);
        boost::context::stack_context sctx;
        sctx.size = size;
        sctx.sp = static_cast<char*>(vp) + sctx.size;
        return sctx;
    }

    void deallocate(boost::context::stack_context& sctx) {
        resource->Deallocate(static_cast<char*>(sctx.sp) - sctx.size, sctx.size);
    }
};

} // namespace

CoroutineImpl::CoroutineImpl(cortex::CoroutineBody body,
                             std::size_t stack_size,
                             const MemoryResourceSharedPtr& resource,
                             bool reusable)
    : reusable_(reusable)
    , stack_size_bytes_(stack_size)
    , body_(std::move(body))
    , fiber_(std::allocator_arg,
             MemoryResourceStackAllocator {resource, stack_size},
             [this](boost::context::fiber&& sink) {
                 started_ = true;
                 FiberSuspendContext suspend_context(sink, this);

                 for (;;) {
                     assert(static_cast<bool>(body_));
                     body_started_ = true;
                     try {
                         body_(suspend_context);
                     } catch (const ForcedUnwind&) {
                         // Unwinding in progress or the body is being aborted
                     } catch (...) {
                         assert(!static_cast<bool>(exception_ptr_));
                         exception_ptr_ = std::current_exception();
                     }

                     is_done_ = true;

                     if (!reusable_) {
                         // Release the body's captures at completion, matching
                         // the pre-trampoline body lifetime.
                         body_ = cortex::CoroutineBody {};
                         break;
                     }
                     if (is_unwinding_) {
                         break;
                     }

                     // Park: give control back and wait for Rebind() +
                     // Resume(), or teardown.
                     sink = std::move(sink).resume();

                     if (is_unwinding_) {
                         break;
                     }
                 }

                 return std::move(sink);
             }) {
    assert(stack_size_bytes_ > 0);
}

CoroutineImpl::~CoroutineImpl() {
    // A started context (mid-body or parked) must be resumed with the
    // unwinding flag set so the trampoline exits and the stack is released.
    // A never-started context must NOT be resumed — that would execute the
    // body during destruction; boost's ~fiber() unwinds it without entering
    // the entry function. A finished one-shot leaves fiber_ empty.
    if (fiber_ && started_) {
        is_unwinding_ = true;
        fiber_ = std::move(fiber_).resume();
    }
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

void CoroutineImpl::Resume() {
    if (IsDone()) {
        throw ResumeOnDoneCoroutineError {"Resume on finished coroutine."};
    }

    fiber_ = std::move(fiber_).resume();

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
    // Discard any leftover exception from an aborted body: its outcome is
    // dropped by definition and must not leak into the next run.
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
    fiber_ = std::move(fiber_).resume();
    abort_body_ = false;
}

} // namespace cortex::detail
