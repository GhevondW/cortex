#pragma once

#include <cstddef>
#include <memory>

#include <cortex/coroutine_body.hpp>

namespace cortex {

namespace detail {
class CoroutineImpl;
}

class Coroutine final {
public:
    static Coroutine Make(CoroutineBody body, std::size_t stack_size_bytes = 262144);

    Coroutine(const Coroutine&) = delete;
    Coroutine(Coroutine&&) noexcept;
    Coroutine& operator=(const Coroutine&) = delete;
    Coroutine& operator=(Coroutine&&) noexcept;
    ~Coroutine();

    [[nodiscard]] std::size_t GetStackSize() const noexcept;
    [[nodiscard]] bool IsDone() const noexcept;
    [[nodiscard]] bool HasException() const noexcept;
    void Resume();

private:
    explicit Coroutine(std::unique_ptr<detail::CoroutineImpl> impl);
    std::unique_ptr<detail::CoroutineImpl> impl_;
};

} // namespace cortex
