#ifndef SRC_CORTEX_INCLUDE_CORTEX_NAIVE_COROUTINE_HPP
#define SRC_CORTEX_INCLUDE_CORTEX_NAIVE_COROUTINE_HPP

#include <cortex/api/flow.hpp>
#include <cortex/api/suspendable.hpp>
#include <cortex/error.hpp>
#include <cortex/execution.hpp>

#include <functional>
#include <memory>

namespace cortex {

/**
 * @brief The `naive_coroutine` class represents a simple coroutine implementation.
 * It is a concrete implementation of the Cortex `flow` interface, providing a way to execute
 * a routine that can be suspended and resumed.
 */
class naive_coroutine : public api::flow {
public:
    /**
     * @brief The `resume_on_completed_coroutine` class represents an exception thrown when trying to resume
     * a coroutine that has already completed its execution.
     */
    struct resume_on_completed_coroutine : public error {
        using error::error;
    };

    /// Type alias for the routine function signature.
    using routine_t = std::function<void(api::suspendable&)>;

private:
    /**
     * @brief Private constructor for creating a `naive_coroutine` with a given routine function.
     * @param routine The routine function representing the execution flow of the coroutine.
     */
    explicit naive_coroutine(routine_t&& routine);

public:
    /**
     * @brief Creates a new `naive_coroutine` with the specified routine function.
     * @param routine The routine function representing the execution flow of the coroutine.
     * @return A new `naive_coroutine` instance.
     */
    static naive_coroutine create(routine_t&& routine);

    /**
     * @brief Creates a unique pointer to a `naive_coroutine` with the specified routine function.
     * @param routine The routine function representing the execution flow of the coroutine.
     * @return A unique pointer to the created `naive_coroutine`.
     */
    static std::unique_ptr<naive_coroutine> make(routine_t&& routine);

    /**
     * @brief Destructor for the `naive_coroutine` class.
     */
    ~naive_coroutine() noexcept override = default;

    naive_coroutine(const naive_coroutine&) = delete;
    naive_coroutine(naive_coroutine&&) = delete;

    naive_coroutine& operator=(const naive_coroutine&) = delete;
    naive_coroutine& operator=(naive_coroutine&&) = delete;

    /**
     * @brief Resumes the execution of the coroutine.
     * @throws resume_on_completed_coroutine if the coroutine has already completed its execution.
     */
    void resume();

    /**
     * @brief Checks whether the coroutine has completed its execution.
     * @return `true` if the coroutine has completed, otherwise `false`.
     */
    [[nodiscard]] bool is_completed() const;

private:
    /**
     * @brief Implementation of the run method from the `flow` interface.
     * @param suspender A reference to a `suspendable` object that can be used to control the coroutine's flow.
     */
    void run(api::suspendable& suspender) override;

private:
    bool _completed; ///< Flag indicating whether the coroutine has completed its execution.
    routine_t _routine; ///< The routine function representing the execution flow of the coroutine.
    execution _exe; ///< The execution context for the coroutine.
};

} // namespace cortex

#endif
