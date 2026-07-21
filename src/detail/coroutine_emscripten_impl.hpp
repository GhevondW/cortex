#pragma once

#include <cstddef>
#include <exception>

#include <emscripten/fiber.h>

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
    [[nodiscard]] emscripten_fiber_t* GetBackFiber() const noexcept;
    void SetBackFiber(emscripten_fiber_t* fiber) noexcept;
    void Resume();

    // Reuse API — see coroutine_native_impl.hpp for the contract.
    void Rebind(cortex::CoroutineBody body);
    void Rebind();
    void AbortBody();

private:
    static void FiberEntry(void* arg);

    // Raw swap into this fiber, bypassing the IsDone guard and exception
    // rethrow. Shared by Resume, AbortBody and the unwinding destructor.
    void SwapIn();

    emscripten_fiber_t fiber_;
    emscripten_fiber_t* back_fiber_ {nullptr};
    cortex::CoroutineBody body_;
    bool is_done_ {false};
    bool is_unwinding_ {false};
    bool abort_body_ {false};
    bool reusable_ {false};
    // The context entered its entry function at least once (destructor must
    // not swap into a never-started context — that would run the body).
    bool started_ {false};
    // The CURRENT body began executing. Distinct from started_: a parked
    // coroutine that was rebound has started_ == true but body_started_ ==
    // false until the next Resume.
    bool body_started_ {false};
    std::size_t stack_size_bytes_;
    std::exception_ptr exception_ptr_ {nullptr};
    MemoryResourceSharedPtr resource_;
    void* c_stack_ {nullptr};
    void* asyncify_stack_ {nullptr};
};

} // namespace cortex::detail
