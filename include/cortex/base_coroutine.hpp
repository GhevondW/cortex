#pragma once

#include <cortex/coroutine.hpp>
#include <cortex/coroutine_suspend_context.hpp>
#include <cortex/memory_resource.hpp>

#include <cstddef>

/**
 * @file base_coroutine.hpp
 * @brief Base class for object-oriented coroutines.
 */

namespace cortex {

/**
 * @class BaseCoroutine
 * @brief An abstract base class for creating object-oriented coroutines.
 *
 * This class provides a way to define coroutines by inheriting from it and
 * implementing the Continuation() method. It wraps the Coroutine class to
 * provide a more object-oriented interface.
 */
class BaseCoroutine {
public:
    /**
     * @brief Pure virtual destructor.
     *
     * Although it is pure virtual, it must have an implementation in the .cpp file.
     */
    virtual ~BaseCoroutine() = 0;

    // The Continuation body captures `this`; the object must never be
    // copied or moved.
    BaseCoroutine(const BaseCoroutine&) = delete;
    BaseCoroutine(BaseCoroutine&&) = delete;
    BaseCoroutine& operator=(const BaseCoroutine&) = delete;
    BaseCoroutine& operator=(BaseCoroutine&&) = delete;

    /**
     * @brief Checks if the coroutine has finished its execution.
     * @return true if execution is complete, false otherwise.
     */
    [[nodiscard]] bool IsDone() const noexcept {
        return coroutine_.IsDone();
    }

    /**
     * @brief Gets the allocated stack size of the underlying coroutine.
     * @return The stack size in bytes.
     */
    [[nodiscard]] std::size_t GetStackSize() const noexcept {
        return coroutine_.GetStackSize();
    }

    /**
     * @brief Resumes the execution of the coroutine.
     *
     * If the coroutine was suspended, it continues from the suspension point.
     * Execution will enter or resume in the Continuation() method.
     */
    void Resume() {
        coroutine_.Resume();
    }

protected:
    /**
     * @brief Constructs a new BaseCoroutine.
     *
     * @param stack_size_bytes The size of the stack to allocate for the coroutine (default: 256KB).
     * @param resource The memory resource to use for stack and implementation allocation (default:
     * GetDefaultMemoryResource()).
     * @param reusable When true, the coroutine parks after Continuation()
     * finishes instead of destroying its context; ResetCoroutineForReuse()
     * re-arms it for another run on the same stack.
     */
    explicit BaseCoroutine(std::size_t stack_size_bytes = Coroutine::kDefaultStackSizeBytes,
                           MemoryResourceSharedPtr resource = GetDefaultMemoryResource(),
                           bool reusable = false);

    /**
     * @brief Re-arms a finished reusable coroutine.
     *
     * The next Resume() runs Continuation() again on the same stack and
     * context. Only valid for reusable coroutines whose Continuation()
     * finished (or never started).
     *
     * @throws std::logic_error if the coroutine was not constructed as
     * reusable, or if Continuation() started but has not finished.
     */
    void ResetCoroutineForReuse();

private:
    /**
     * @brief The entry point for the coroutine execution.
     *
     * Subclasses must implement this method to define the coroutine's behavior.
     * @param self The context used to suspend the coroutine.
     */
    virtual void Continuation(CoroutineSuspendContext& self) = 0;

private:
    Coroutine coroutine_;
};

} // namespace cortex
