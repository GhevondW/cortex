#pragma once

/**
 * @file coroutine_suspend_context.hpp
 * @brief Context for suspending coroutine execution.
 */

namespace cortex {

/**
 * @class CoroutineSuspendContext
 * @brief Provides a mechanism for a coroutine to suspend itself.
 *
 * This class is passed to the coroutine body and contains the Suspend()
 * method to yield control back to the caller.
 */
class CoroutineSuspendContext {
public:
    static void* operator new(std::size_t) = delete;
    static void* operator new[](std::size_t) = delete;

public:
    CoroutineSuspendContext(const CoroutineSuspendContext&) = delete;
    CoroutineSuspendContext(CoroutineSuspendContext&&) = delete;
    CoroutineSuspendContext& operator=(const CoroutineSuspendContext&) = delete;
    CoroutineSuspendContext& operator=(CoroutineSuspendContext&&) = delete;

    /**
     * @brief Suspends the current coroutine's execution.
     *
     * Control returns to the point where Coroutine::Resume() was called.
     * Execution will resume from this point when Resume() is called again.
     */
    virtual void Suspend() = 0;

protected:
    virtual ~CoroutineSuspendContext() = default;
    CoroutineSuspendContext() = default;
};

} // namespace cortex
