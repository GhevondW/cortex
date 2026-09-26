#pragma once

/**
 * @file task.hpp
 * @brief Task<T> — handle to a spawned fiber's result.
 */

#include <chrono>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace cortex::async {

class Executor;

/**
 * @class Task
 * @brief Handle to a spawned fiber's result.
 *
 * Task<T> represents the result of a fiber spawned via Spawn() or
 * Executor::Spawn(). It supports waiting, cancellation, detachment,
 * and continuations.
 *
 * - Move-only (non-copyable).
 * - Destructor blocks until completion if not awaited or detached.
 * - Get() moves the result out; can only be called once.
 *
 * @tparam T The result type of the fiber.
 */
template <typename T>
class Task {
public:
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    Task(Task&& other) noexcept
        : state_(std::move(other.state_)) {}

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            state_ = std::move(other.state_);
        }
        return *this;
    }

    ~Task() = default;

    /**
     * @brief Block current fiber until the task completes.
     *
     * If called from outside a fiber, blocks the OS thread.
     */
    void Wait() {
        throw std::runtime_error("Not implemented yet");
    }

    /**
     * @brief Wait with timeout.
     * @return true if completed, false if timed out.
     */
    template <typename Rep, typename Period>
    bool WaitFor([[maybe_unused]] std::chrono::duration<Rep, Period> timeout) {
        throw std::runtime_error("Not implemented yet");
    }

    /**
     * @brief Get the result. Blocks until complete. Rethrows exceptions.
     *
     * Can only be called once (value is moved out).
     */
    T Get() {
        throw std::runtime_error("Not implemented yet");
    }

    /**
     * @brief Check if the task has completed.
     */
    [[nodiscard]] bool IsReady() const noexcept {
        return false;
    }

    /**
     * @brief Check if the task was cancelled.
     */
    [[nodiscard]] bool IsCancelled() const noexcept {
        return false;
    }

    /**
     * @brief Request cancellation.
     *
     * The fiber will see cancellation at the next cancellation point
     * (yield, wait, sleep).
     */
    void Cancel() {
        throw std::runtime_error("Not implemented yet");
    }

    /**
     * @brief Detach the task.
     *
     * The fiber continues running but this handle no longer tracks it.
     * Destructor becomes a no-op.
     */
    void Detach() {
        throw std::runtime_error("Not implemented yet");
    }

    /**
     * @brief Attach a continuation that runs when this task completes.
     *
     * The continuation runs on the same executor. Returns a new Task.
     */
    template <typename F>
    auto Then([[maybe_unused]] F&& continuation) -> Task<std::invoke_result_t<F, T>> {
        throw std::runtime_error("Not implemented yet");
    }

private:
    friend class Executor;

    struct State {};
    explicit Task(std::shared_ptr<State> state)
        : state_(std::move(state)) {}
    std::shared_ptr<State> state_;
};

/**
 * @brief Specialization of Task for void return type.
 */
template <>
class Task<void> {
public:
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    Task(Task&& other) noexcept
        : state_(std::move(other.state_)) {}

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            state_ = std::move(other.state_);
        }
        return *this;
    }

    ~Task() = default;

    void Wait() {
        throw std::runtime_error("Not implemented yet");
    }

    template <typename Rep, typename Period>
    bool WaitFor([[maybe_unused]] std::chrono::duration<Rep, Period> timeout) {
        throw std::runtime_error("Not implemented yet");
    }

    void Get() {
        throw std::runtime_error("Not implemented yet");
    }

    [[nodiscard]] bool IsReady() const noexcept {
        return false;
    }

    [[nodiscard]] bool IsCancelled() const noexcept {
        return false;
    }

    void Cancel() {
        throw std::runtime_error("Not implemented yet");
    }

    void Detach() {
        throw std::runtime_error("Not implemented yet");
    }

    template <typename F>
    auto Then([[maybe_unused]] F&& continuation) -> Task<std::invoke_result_t<F>> {
        throw std::runtime_error("Not implemented yet");
    }

private:
    friend class Executor;

    struct State {};
    explicit Task(std::shared_ptr<State> state)
        : state_(std::move(state)) {}
    std::shared_ptr<State> state_;
};

} // namespace cortex::async
