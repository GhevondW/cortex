#pragma once

/**
 * @file future.hpp
 * @brief Future<T> / Promise<T> — async value passing between fibers.
 */

#include <chrono>
#include <exception>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace cortex::async {

// Forward declarations
template <typename T>
class Future;

/**
 * @class Promise
 * @brief Producer side of an async value.
 *
 * Create a Promise, obtain its Future via GetFuture(), then fulfill
 * the promise with SetValue() or SetException(). The waiting fiber
 * is automatically woken.
 *
 * @tparam T The value type.
 */
template <typename T>
class Promise {
public:
    Promise()
        : state_(std::make_shared<SharedState>()) {}
    ~Promise() = default;

    Promise(const Promise&) = delete;
    Promise& operator=(const Promise&) = delete;

    Promise(Promise&& other) noexcept
        : state_(std::move(other.state_)) {}

    Promise& operator=(Promise&& other) noexcept {
        if (this != &other) {
            state_ = std::move(other.state_);
        }
        return *this;
    }

    /**
     * @brief Get the associated future. Can only be called once.
     */
    Future<T> GetFuture() {
        throw std::runtime_error("Not implemented yet");
    }

    /**
     * @brief Fulfill the promise with a value.
     */
    template <typename U>
    void SetValue([[maybe_unused]] U&& value) {
        throw std::runtime_error("Not implemented yet");
    }

    /**
     * @brief Fulfill the promise with an exception.
     */
    void SetException([[maybe_unused]] std::exception_ptr ex) {
        throw std::runtime_error("Not implemented yet");
    }

    /**
     * @brief Check if the promise has already been fulfilled.
     */
    [[nodiscard]] bool IsFulfilled() const noexcept {
        return false;
    }

private:
    struct SharedState {};
    std::shared_ptr<SharedState> state_;
};

/**
 * @class Future
 * @brief Consumer side of an async value.
 *
 * Obtained from a Promise via GetFuture(). Suspends the current fiber
 * when waiting, rather than blocking the OS thread.
 *
 * @tparam T The value type.
 */
template <typename T>
class Future {
public:
    Future(const Future&) = delete;
    Future& operator=(const Future&) = delete;

    Future(Future&& other) noexcept
        : state_(std::move(other.state_)) {}

    Future& operator=(Future&& other) noexcept {
        if (this != &other) {
            state_ = std::move(other.state_);
        }
        return *this;
    }

    ~Future() = default;

    /**
     * @brief Block current fiber until the value is ready.
     */
    void Wait() {
        throw std::runtime_error("Not implemented yet");
    }

    /**
     * @brief Wait with timeout.
     * @return true if ready, false if timed out.
     */
    template <typename Rep, typename Period>
    bool WaitFor([[maybe_unused]] std::chrono::duration<Rep, Period> timeout) {
        throw std::runtime_error("Not implemented yet");
    }

    /**
     * @brief Get the value. Blocks until ready. Rethrows exceptions.
     */
    T Get() {
        throw std::runtime_error("Not implemented yet");
    }

    /**
     * @brief Check if the value is ready (non-blocking).
     */
    [[nodiscard]] bool IsReady() const noexcept {
        return false;
    }

    /**
     * @brief Attach a continuation.
     */
    template <typename F>
    auto Then([[maybe_unused]] F&& func) -> Future<std::invoke_result_t<F, T>> {
        throw std::runtime_error("Not implemented yet");
    }

private:
    friend class Promise<T>;

    struct SharedState {};
    explicit Future(std::shared_ptr<SharedState> state)
        : state_(std::move(state)) {}
    std::shared_ptr<SharedState> state_;
};

// --- void specializations ---

template <>
class Promise<void> {
public:
    Promise();
    ~Promise();

    Promise(const Promise&) = delete;
    Promise& operator=(const Promise&) = delete;
    Promise(Promise&&) noexcept;
    Promise& operator=(Promise&&) noexcept;

    Future<void> GetFuture();
    void SetValue();
    void SetException(std::exception_ptr ex);
    [[nodiscard]] bool IsFulfilled() const noexcept;

private:
    struct SharedState;
    std::shared_ptr<SharedState> state_;
};

template <>
class Future<void> {
public:
    Future(const Future&) = delete;
    Future& operator=(const Future&) = delete;
    Future(Future&&) noexcept;
    Future& operator=(Future&&) noexcept;
    ~Future();

    void Wait();

    template <typename Rep, typename Period>
    bool WaitFor([[maybe_unused]] std::chrono::duration<Rep, Period> timeout) {
        throw std::runtime_error("Not implemented yet");
    }

    void Get();
    [[nodiscard]] bool IsReady() const noexcept;

    template <typename F>
    auto Then([[maybe_unused]] F&& func) -> Future<std::invoke_result_t<F>> {
        throw std::runtime_error("Not implemented yet");
    }

private:
    friend class Promise<void>;

    struct SharedState;
    explicit Future(std::shared_ptr<SharedState> state);
    std::shared_ptr<SharedState> state_;
};

// --- Factory helpers ---

/**
 * @brief Create an already-fulfilled future with a value.
 */
template <typename T>
Future<T> MakeReadyFuture([[maybe_unused]] T&& value) {
    throw std::runtime_error("Not implemented yet");
}

/**
 * @brief Create an already-fulfilled void future.
 */
inline Future<void> MakeReadyFuture() {
    throw std::runtime_error("Not implemented yet");
}

/**
 * @brief Create a future fulfilled with an exception.
 */
template <typename T>
Future<T> MakeExceptionalFuture([[maybe_unused]] std::exception_ptr ex) {
    throw std::runtime_error("Not implemented yet");
}

} // namespace cortex::async
