#pragma once

#include <cstddef>
#include <exception>
#include <vector>

#include <emscripten/fiber.h>

#include <cortex/coroutine_body.hpp>

namespace cortex::detail {

class Coroutine final {
public:
    // Use after move is UB for this library
    static Coroutine Make(CoroutineBody body, std::size_t stack_size_bytes = 262144);

    Coroutine(const Coroutine&) = delete;
    Coroutine(Coroutine&&) noexcept = default;
    Coroutine& operator=(const Coroutine&) = delete;
    Coroutine& operator=(Coroutine&&) noexcept = default;
    ~Coroutine();

    [[nodiscard]] std::size_t GetStackSize() const noexcept;

    [[nodiscard]] bool IsDone() const noexcept;

    [[nodiscard]] bool HasException() const noexcept;

    // @throws ResumeOnDoneCoroutineError
    void Resume();

private:
    explicit Coroutine(CoroutineBody body, std::size_t stack_size_bytes);

    bool is_done_ {false};
    bool is_unwinding_ {false};
    std::size_t stack_size_bytes_ {};
    std::exception_ptr exception_ptr_ {};
    CoroutineBody body_ {};

    emscripten_fiber_t fiber_ {};
    std::vector<char> c_stack_ {};
    std::vector<char> asyncify_stack_ {};

    static void FiberEntry(void* arg);
};

} // namespace cortex::detail
