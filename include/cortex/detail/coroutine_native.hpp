#pragma once

#include <cstddef>
#include <exception>

#include <boost/context/fiber.hpp>

#include <cortex/coroutine_body.hpp>

namespace cortex::detail {

class Coroutine final {
public:
    // Use after move is UB for this library
    // TODO : allocator support right now this stack_size_bytes is not used
    static Coroutine Make(cortex::CoroutineBody body, std::size_t stack_size_bytes = 262144);

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
    explicit Coroutine(cortex::CoroutineBody body, std::size_t stack_size_bytes);

    bool is_done_;
    bool is_unwinding_ = false;
    std::size_t stack_size_bytes_;
    std::exception_ptr exception_ptr_;
    boost::context::fiber fiber_;
};

} // namespace cortex::detail
