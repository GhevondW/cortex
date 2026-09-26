#pragma once

#include <cstddef>
#include <exception>

#include <emscripten/fiber.h>

#include <cortex/coroutine_body.hpp>
#include <cortex/memory_resource.hpp>

namespace cortex::detail {

class CoroutineImpl final {
public:
    CoroutineImpl(cortex::CoroutineBody body, std::size_t stack_size, const MemoryResourceSharedPtr& resource);
    ~CoroutineImpl();

    [[nodiscard]] std::size_t GetStackSize() const noexcept;
    [[nodiscard]] bool IsDone() const noexcept;
    [[nodiscard]] bool HasException() const noexcept;
    [[nodiscard]] bool IsUnwinding() const noexcept;
    [[nodiscard]] emscripten_fiber_t* GetBackFiber() const noexcept;
    void SetBackFiber(emscripten_fiber_t* fiber) noexcept;
    void Resume();

    // Called by the coroutine each time it gets control (first entry, or
    // back from a Suspend()).
    void OnSwitchedIn() noexcept;
    // Called by the coroutine right before it hands control back to the
    // context that resumed it; `finished` when it will never run again.
    void OnSwitchingOut(bool finished) noexcept;

private:
    static void FiberEntry(void* arg);

    emscripten_fiber_t fiber_;
    emscripten_fiber_t* back_fiber_ {nullptr};
    cortex::CoroutineBody body_;
    bool is_done_ {false};
    bool is_unwinding_ {false};
    // Set by the coroutine each time it gets control, so Resume() can tell a
    // switch that never happened (the Emscripten runtime has exited) from
    // one that did.
    bool switched_in_ {false};
    // AddressSanitizer bookkeeping (unused without ASan): ASan must be told
    // about every stack switch, or an exception unwinding a coroutine stack
    // leaves stale poison behind that a later coroutine reusing the memory
    // trips over. `back_stack_*` is the stack of the context that resumed us.
    [[maybe_unused]] void* asan_fake_stack_ {nullptr};
    [[maybe_unused]] const void* back_stack_bottom_ {nullptr};
    [[maybe_unused]] std::size_t back_stack_size_ {0};
    std::size_t stack_size_bytes_;
    std::exception_ptr exception_ptr_ {nullptr};
    MemoryResourceSharedPtr resource_;
    void* c_stack_ {nullptr};
    void* asyncify_stack_ {nullptr};
};

} // namespace cortex::detail
