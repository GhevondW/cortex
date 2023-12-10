#ifndef SRC_CORTEX_INCLUDE_CORTEX_COROUTINE_HPP
#define SRC_CORTEX_INCLUDE_CORTEX_COROUTINE_HPP

#include <cortex/api/suspendable.hpp>
#include <cortex/error.hpp>
#include <cortex/execution.hpp>
#include <cortex/stack_allocator.hpp>

#include <functional>
#include <memory>

namespace cortex {

/**
 * @brief Class representing a coroutine.
 */
class coroutine : public api::flow {
public:
    /**
     * @brief Exception thrown when attempting to resume a completed coroutine.
     */
    struct resume_on_completed_coroutine : public error {
        using error::error;
    };

    /**
     * @brief Exception thrown when attempting to suspend a not started coroutine.
     */
    struct suspend_on_not_started_coroutine : public error {
        using error::error;
    };

    /**
     * @brief Interface for the routine of the coroutine.
     */
    struct routine_i {
        routine_i() = default;
        virtual ~routine_i() noexcept = default;

        /**
         * @brief Pure virtual function to run the routine.
         */
        virtual void run_routine() = 0;
    };

    /**
     * @brief Concrete implementation of routine_i with a function.
     */
    class basic_routine : public routine_i {
        explicit basic_routine(std::function<void()>&& func);

    public:
        /**
         * @brief Factory method to create a basic_routine.
         * @param func The function to be executed in the routine.
         * @return A unique pointer to the created basic_routine.
         */
        static std::unique_ptr<basic_routine> make(std::function<void()>&& func);

        ~basic_routine() noexcept override = default;

        void run_routine() override;

    private:
        std::function<void()> _func;
    };

private:
    /**
     * @brief Constructor for the coroutine.
     * @param alloc The stack allocator for the coroutine.
     * @param routine The routine to be executed by the coroutine.
     */
    coroutine(cortex::stack_allocator alloc, routine_i* routine);

public:
    /**
     * @brief Factory method to create a coroutine with a stack allocator and routine.
     * @param alloc The stack allocator for the coroutine.
     * @param routine The routine to be executed by the coroutine.
     * @return A coroutine object.
     * @throws invalid_argument_error if the input routine is nullptr.
     */
    static coroutine create(cortex::stack_allocator&& alloc, routine_i* routine);

    /**
     * @brief Factory method to create a coroutine with a routine.
     * @param routine The routine to be executed by the coroutine.
     * @return A coroutine object.
     * @throws invalid_argument_error if the input routine is nullptr.
     */
    static coroutine create(routine_i* routine);

    /**
     * @brief Factory method to create a basic_routine.
     * @param func The function to be executed in the routine.
     * @return A unique pointer to the created basic_routine.
     * @throws invalid_argument_error if the input func is nullptr.
     */
    static std::unique_ptr<basic_routine> make_routine(std::function<void()>&& func);

    ~coroutine() noexcept override = default;

    coroutine(const coroutine&) = delete;
    coroutine(coroutine&&) = delete;

    coroutine& operator=(const coroutine&) = delete;
    coroutine& operator=(coroutine&&) = delete;

    /**
     * @brief Resumes the execution of the coroutine.
     * @throws resume_on_completed_coroutine if the coroutine has already completed.
     */
    void resume();

    /**
     * @brief Suspends the execution of the coroutine.
     * @throws suspend_on_not_started_coroutine if the coroutine has not started yet.
     */
    void suspend();

    /**
     * @brief Checks if the coroutine has completed.
     * @return True if the coroutine has completed, false otherwise.
     */
    [[nodiscard]] bool is_completed() const;

private:
    void run(api::suspendable& suspender) override;

public:
    bool _completed {false};
    routine_i* _routine {nullptr};
    api::suspendable* _suspender = nullptr;
    execution _exec;
};

} // namespace cortex

#endif
