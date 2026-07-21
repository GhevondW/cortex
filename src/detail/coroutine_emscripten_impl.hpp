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

private:
    static void FiberEntry(void* arg);

    emscripten_fiber_t fiber_;
    emscripten_fiber_t* back_fiber_ {nullptr};
    cortex::CoroutineBody body_;
    bool is_done_ {false};
    bool is_unwinding_ {false};
    std::size_t stack_size_bytes_;
    std::exception_ptr exception_ptr_ {nullptr};
    MemoryResourceSharedPtr resource_;
    void* c_stack_ {nullptr};
    void* asyncify_stack_ {nullptr};
};

} // namespace cortex::detail
