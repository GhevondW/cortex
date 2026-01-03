#pragma once

#include <cstddef>
#include <memory>

#include <cortex/coroutine_body.hpp>
#include <cortex/memory_resource.hpp>

/**
 * @file coroutine.hpp
 * @brief Main entry point for the cortex coroutine library.
 */

namespace cortex {

namespace detail {
class CoroutineImpl;
}

/**
 * @class Coroutine
 * @brief A stackful coroutine that provides a mechanism for cooperative multitasking.
 *
 * The Coroutine class manages a separate execution stack and allows suspending
 * and resuming execution. It follows the PIMPL pattern to provide a unified
 * API across different platforms (Native and Emscripten).
 */
class Coroutine final {
public:
    /**
     * @brief Creates a new coroutine with the specified body and stack size.
     *
     * @param body The function or callable to execute within the coroutine.
     * @param stack_size_bytes The size of the stack to allocate for the coroutine (default: 256KB).
     * @param resource The memory resource to use for stack and implementation allocation (default:
     * GetDefaultMemoryResource()).
     * @return A Coroutine instance.
     * @throws std::invalid_argument if the body is empty or stack_size_bytes is 0 or resource is null.
     */
    static Coroutine Make(CoroutineBody body,
                          std::size_t stack_size_bytes = 262144,
                          MemoryResourceSharedPtr resource = GetDefaultMemoryResource());

    /**
     * @struct Builder
     * @brief A builder class for creating Coroutine instances with custom configuration.
     *
     * The Builder allows setting the stack size and memory resource before
     * constructing the coroutine.
     */
    struct Builder {
    public:
        /**
         * @brief Default constructor for Builder.
         *
         * Initializes with default stack size (256KB) and default memory resource.
         */
        Builder();

        /**
         * @brief Builds and returns a new Coroutine instance.
         *
         * @param body The function or callable to execute within the coroutine.
         * @return A new Coroutine instance.
         * @throws std::invalid_argument if the body is empty or stack_size_bytes is 0 or resource is null.
         */
        Coroutine Build(CoroutineBody body) &&;

        /**
         * @brief Sets the stack size for the coroutine to be built.
         *
         * @param stack_size_bytes The size of the stack in bytes.
         * @return The builder instance for chaining.
         */
        Builder SetStackSizeInBytes(std::size_t stack_size_bytes) && noexcept;

        /**
         * @brief Sets the memory resource for the coroutine to be built.
         *
         * @param resource The memory resource to use.
         * @return The builder instance for chaining.
         */
        Builder SetMemoryResource(MemoryResourceSharedPtr resource) && noexcept;

    private:
        std::size_t stack_size_bytes_ {0};
        MemoryResourceSharedPtr memory_resource_ {nullptr};
    };

    Coroutine(const Coroutine&) = delete;
    Coroutine(Coroutine&&) noexcept;
    Coroutine& operator=(const Coroutine&) = delete;
    Coroutine& operator=(Coroutine&&) noexcept;
    ~Coroutine();

    /**
     * @brief Gets the allocated stack size of the coroutine.
     * @return The stack size in bytes.
     */
    [[nodiscard]] std::size_t GetStackSize() const noexcept;

    /**
     * @brief Checks if the coroutine has finished its execution.
     * @return true if execution is complete, false otherwise.
     */
    [[nodiscard]] bool IsDone() const noexcept;

    /**
     * @brief Resumes the execution of the coroutine.
     *
     * If the coroutine was suspended, it continues from the suspension point.
     * If an exception was caught inside the coroutine, it will be rethrown here.
     *
     * @throws ResumeOnDoneCoroutineError if attempting to resume a finished coroutine.
     */
    void Resume();

private:
    struct ImplDeleter {
        MemoryResourceSharedPtr resource;
        void operator()(detail::CoroutineImpl* impl) const;
    };

    explicit Coroutine(std::unique_ptr<detail::CoroutineImpl, ImplDeleter> impl);
    std::unique_ptr<detail::CoroutineImpl, ImplDeleter> impl_;
};

} // namespace cortex
