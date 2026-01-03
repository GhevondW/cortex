#pragma once

#include <cstddef>
#include <exception>

#include <boost/context/fiber.hpp>

#include <cortex/coroutine_body.hpp>
#include <cortex/memory_resource.hpp>

namespace cortex::detail {

class CoroutineImpl final {
public:
    CoroutineImpl(cortex::CoroutineBody body, std::size_t stack_size, MemoryResourceSharedPtr resource);
    ~CoroutineImpl();

    [[nodiscard]] std::size_t GetStackSize() const noexcept;
    [[nodiscard]] bool IsDone() const noexcept;
    [[nodiscard]] bool HasException() const noexcept;
    [[nodiscard]] bool IsUnwinding() const noexcept;
    void Resume();

private:
    bool is_done_ {false};
    bool is_unwinding_ {false};
    std::size_t stack_size_bytes_;
    std::exception_ptr exception_ptr_ {nullptr};
    boost::context::fiber fiber_;
};

} // namespace cortex::detail
