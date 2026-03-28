#pragma once

#include <cortex/fiber/detail/platform.hpp>
#include <cortex/fiber/scheduler.hpp>
#include <cortex/fiber/yield.hpp>

#include <atomic>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace cortex::fiber {

namespace detail {

template <typename T>
struct FutureState {
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<bool> ready {false};
    std::atomic<bool> retrieved {false};
    std::optional<T> result;
    std::exception_ptr exception;
};

template <>
struct FutureState<void> {
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<bool> ready {false};
    std::atomic<bool> retrieved {false};
    std::exception_ptr exception;
};

} // namespace detail

template <typename T>
class Future {
public:
    Future() = default;

    Future(const Future&) = delete;
    Future& operator=(const Future&) = delete;

    Future(Future&&) noexcept = default;
    Future& operator=(Future&&) noexcept = default;

    ~Future() {
        if (state_ && !state_->retrieved.load(std::memory_order_acquire)) {
            try {
                WaitImpl();
            } catch (...) {
                // Never throw from destructor.
            }
        }
    }

    void Wait() {
        if (!state_) {
            throw std::logic_error("Future has no state");
        }
        WaitImpl();
    }

    T Get() {
        if (!state_) {
            throw std::logic_error("Future has no state");
        }

        WaitImpl();

        bool expected = false;
        if (!state_->retrieved.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            throw std::logic_error("Future result already retrieved");
        }

        if (state_->exception) {
            std::rethrow_exception(state_->exception);
        }

        if (!state_->result.has_value()) {
            throw std::logic_error("Fiber completed without result");
        }

        return std::move(*state_->result);
    }

    [[nodiscard]] bool IsReady() const noexcept {
        return !state_ || state_->ready.load(std::memory_order_acquire);
    }

private:
    template <typename F>
    friend auto Spawn(F&& func) -> Future<std::invoke_result_t<F>>;
    template <typename F>
    friend auto Spawn(F&& func, std::size_t stack_size) -> Future<std::invoke_result_t<F>>;

    explicit Future(std::shared_ptr<detail::FutureState<T>> state)
        : state_(std::move(state)) {}

    void WaitImpl() const {
        if (!state_) {
            return;
        }

        if (state_->ready.load(std::memory_order_acquire)) {
            return;
        }

        if (Scheduler::TryCurrent() != nullptr) {
            while (!state_->ready.load(std::memory_order_acquire)) {
                Yield();
            }
            return;
        }

        std::unique_lock lock(state_->mutex);
        state_->cv.wait(lock, [this] {
            return state_->ready.load(std::memory_order_acquire);
        });
    }

    std::shared_ptr<detail::FutureState<T>> state_;
};

template <>
class Future<void> {
public:
    Future() = default;

    Future(const Future&) = delete;
    Future& operator=(const Future&) = delete;

    Future(Future&&) noexcept = default;
    Future& operator=(Future&&) noexcept = default;

    ~Future() {
        if (state_ && !state_->retrieved.load(std::memory_order_acquire)) {
            try {
                WaitImpl();
            } catch (...) {
                // Never throw from destructor.
            }
        }
    }

    void Wait() {
        if (!state_) {
            throw std::logic_error("Future has no state");
        }

        WaitImpl();

        bool expected = false;
        if (!state_->retrieved.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            throw std::logic_error("Future result already retrieved");
        }

        if (state_->exception) {
            std::rethrow_exception(state_->exception);
        }
    }

    [[nodiscard]] bool IsReady() const noexcept {
        return !state_ || state_->ready.load(std::memory_order_acquire);
    }

private:
    template <typename F>
    friend auto Spawn(F&& func) -> Future<std::invoke_result_t<F>>;
    template <typename F>
    friend auto Spawn(F&& func, std::size_t stack_size) -> Future<std::invoke_result_t<F>>;

    explicit Future(std::shared_ptr<detail::FutureState<void>> state)
        : state_(std::move(state)) {}

    void WaitImpl() const {
        if (!state_) {
            return;
        }

        if (state_->ready.load(std::memory_order_acquire)) {
            return;
        }

        if (Scheduler::TryCurrent() != nullptr) {
            while (!state_->ready.load(std::memory_order_acquire)) {
                Yield();
            }
            return;
        }

        std::unique_lock lock(state_->mutex);
        state_->cv.wait(lock, [this] {
            return state_->ready.load(std::memory_order_acquire);
        });
    }

    std::shared_ptr<detail::FutureState<void>> state_;
};

template <typename F>
auto Spawn(F&& func, std::size_t stack_size) -> Future<std::invoke_result_t<F>> {
    using ResultType = std::invoke_result_t<F>;

    auto& scheduler = Scheduler::Current();
    auto state = std::make_shared<detail::FutureState<ResultType>>();

    scheduler.SpawnFiberInternal(
        [state, f = std::forward<F>(func)]() mutable {
            try {
                if constexpr (std::is_void_v<ResultType>) {
                    f();
                } else {
                    state->result = f();
                }
            } catch (...) {
                state->exception = std::current_exception();
            }

            state->ready.store(true, std::memory_order_release);
            state->cv.notify_all();
        },
        stack_size);

    return Future<ResultType>(std::move(state));
}

template <typename F>
auto Spawn(F&& func) -> Future<std::invoke_result_t<F>> {
    return Spawn(std::forward<F>(func), Scheduler::Current().GetDefaultStackSize());
}

} // namespace cortex::fiber
