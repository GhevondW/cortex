#pragma once

#include <cstddef>
#include <exception>
#include <memory>

#include <cortex/coroutine_body.hpp>

namespace cortex::detail {

class Coroutine final {
public:
    struct Impl;

    // Use after move is UB for this library
    static Coroutine Make(CoroutineBody body, std::size_t stack_size_bytes = 262144);

    Coroutine(const Coroutine&) = delete;
    Coroutine(Coroutine&&) noexcept;
    Coroutine& operator=(const Coroutine&) = delete;
    Coroutine& operator=(Coroutine&&) noexcept;
    ~Coroutine();

    [[nodiscard]] std::size_t GetStackSize() const noexcept;

    [[nodiscard]] bool IsDone() const noexcept;

    [[nodiscard]] bool HasException() const noexcept;

    // @throws ResumeOnDoneCoroutineError
    void Resume();

private:
    explicit Coroutine(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

} // namespace cortex::detail
