#pragma once

#include <cstddef>
#include <memory>

#include <cortex/coroutine_body.hpp>

namespace cortex::detail {

// Use after move is UB for this library

class Coroutine final {
public:
    struct Impl;

    static Coroutine Make(cortex::CoroutineBody body, std::size_t stack_size_bytes = 262144);

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
