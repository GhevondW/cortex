#pragma once

#include <cstddef>
#include <exception>

#include <boost/context/fiber.hpp>

#include <cortex/coroutine_body.hpp>
#include <cortex/memory_resource.hpp>

namespace cortex::detail {

class CoroutineImpl final {
public:
    CoroutineImpl(cortex::CoroutineBody body,
                  std::size_t stack_size,
                  const MemoryResourceSharedPtr& resource,
                  bool reusable = false);
    ~CoroutineImpl();

    [[nodiscard]] std::size_t GetStackSize() const noexcept;
    [[nodiscard]] bool IsDone() const noexcept;
    [[nodiscard]] bool HasException() const noexcept;
    [[nodiscard]] bool IsUnwinding() const noexcept;
    [[nodiscard]] bool ShouldAbortBody() const noexcept;
    void Resume();

    // Reuse API. Only valid when constructed with reusable == true: a
    // reusable coroutine parks after its body finishes instead of letting
    // the context die.

    // Replace the body and arm the coroutine for another run. Valid when the
    // previous body finished or the coroutine never started.
    // @throws std::logic_error if a started body has not finished.
    void Rebind(cortex::CoroutineBody body);

    // Re-arm with the current body. Used by BaseCoroutine reuse, where the
    // body is always `[this](ctx) { Continuation(ctx); }`.
    // @throws std::logic_error if a started body has not finished.
    void Rebind();

    // Force-unwind a started-but-unfinished body and park the trampoline so
    // the coroutine can be rebound. No-op if the body already finished or
    // never started.
    void AbortBody();

private:
    bool is_done_ {false};
    bool is_unwinding_ {false};
    bool abort_body_ {false};
    bool reusable_ {false};
    bool started_ {false};
    std::size_t stack_size_bytes_;
    cortex::CoroutineBody body_;
    std::exception_ptr exception_ptr_ {nullptr};
    boost::context::fiber fiber_;
};

} // namespace cortex::detail
