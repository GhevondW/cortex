#include "detail/coroutine_native_impl.hpp"

#include <cassert>
#include <utility>

#include "cortex/coroutine_suspend_context.hpp"
#include "cortex/errors/resume_on_completed_coroutine_error.hpp"
#include "forced_unwind.hpp"

namespace cortex::detail {

namespace {

struct FiberSuspendContext final : cortex::CoroutineSuspendContext {
    explicit FiberSuspendContext(boost::context::fiber& sink, CoroutineImpl* impl)
        : _sink(sink)
        , _impl(impl) {}

    ~FiberSuspendContext() override = default;

    void Suspend() override {
        _sink = std::move(_sink).resume();
        if (_impl->IsUnwinding()) {
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

CoroutineImpl::CoroutineImpl(cortex::CoroutineBody body, std::size_t stack_size, MemoryResourceSharedPtr resource)
    : stack_size_bytes_(stack_size)
    , fiber_(std::allocator_arg,
             MemoryResourceStackAllocator {resource, stack_size},
             [this, body = std::move(body)](boost::context::fiber&& sink) mutable {
                 assert(static_cast<bool>(body));
                 FiberSuspendContext suspend_context(sink, this);

                 try {
                     body(suspend_context);
                 } catch (const ForcedUnwind&) {
                     // Unwinding in progress
                 } catch (...) {
                     assert(!static_cast<bool>(exception_ptr_));
                     exception_ptr_ = std::current_exception();
                 }

                 is_done_ = true;

                 return std::move(sink);
             }) {
    assert(stack_size_bytes_ > 0);
}

CoroutineImpl::~CoroutineImpl() {
    if (!is_done_ && fiber_) {
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

} // namespace cortex::detail
