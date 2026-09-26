#pragma once

#include <cortex/tiny_fiber/detail/future_state.hpp>
#include <cortex/tiny_fiber/future.hpp>
#include <cortex/tiny_fiber/scheduler.hpp>

#include <exception>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

/**
 * @file promise.hpp
 * @brief A result that plain code (or another fiber) delivers to fibers.
 */

namespace cortex::tiny_fiber {

/**
 * @class Promise
 * @brief The producing side of a Future that is not tied to a fiber.
 *
 * Fibers wait on GetFuture(); anything on the scheduler's thread fulfils it —
 * another fiber, or plain code such as a JavaScript callback between
 * scheduler steps (the scheduler's wake-up handler then fires). From other
 * threads, fulfil it through Scheduler::Post().
 *
 * While fibers wait on a promise the scheduler reports Status::kWaiting, not
 * kDeadlocked: the result is expected to come from outside. A blocking
 * Scheduler::Run() therefore keeps waiting for it (like std::future::wait)
 * instead of throwing DeadlockError — fulfil it via Post() from another
 * thread, or use Create() + RunFor() when it comes from an event loop. (In
 * single-threaded WebAssembly nothing can arrive while Run() blocks, so there
 * Run() throws DeadlockError instead.) Destroying an unfulfilled promise
 * completes its future with BrokenPromiseError. A promise may be fulfilled
 * even after its scheduler is gone; the future then simply holds the result.
 *
 * A promise belongs to its scheduler's thread: create, fulfil and destroy it
 * there. Another thread hands it over with Scheduler::Post(), moving the
 * promise into the posted work.
 *
 * @tparam T The value type (may be void).
 */
template <typename T>
class Promise {
public:
    /**
     * @brief A promise on the current scheduler (call from a fiber).
     */
    Promise()
        : Promise(Scheduler::Current()) {}

    /**
     * @brief A promise on `scheduler` (usable from plain code).
     */
    explicit Promise(Scheduler& scheduler)
        : state_(detail::MakeFutureState<T>(scheduler, /*external=*/true)) {}

    Promise(const Promise&) = delete;
    Promise& operator=(const Promise&) = delete;

    /// Takes over `other`'s future; `other` is left empty.
    Promise(Promise&& other) noexcept
        : state_(std::move(other.state_))
        , future_taken_(other.future_taken_) {}

    /// Abandons this promise's future if unfulfilled (BrokenPromiseError),
    /// then takes over `other`'s.
    Promise& operator=(Promise&& other) noexcept {
        if (this != &other) {
            AbandonIfUnfulfilled();
            state_ = std::move(other.state_);
            future_taken_ = other.future_taken_;
        }
        return *this;
    }

    ~Promise() {
        AbandonIfUnfulfilled();
    }

    /**
     * @brief The future fibers wait on. Can be taken once.
     * @throws std::logic_error on the second call.
     */
    Future<T> GetFuture() {
        RequireState();
        if (future_taken_) {
            throw std::logic_error("Promise::GetFuture() already called");
        }
        future_taken_ = true;
        return Future<T>(state_);
    }

    /**
     * @brief Deliver the value and wake the waiting fibers.
     * @throws std::logic_error if already fulfilled.
     */
    template <typename U = T>
        requires(!std::is_void_v<T>)
    void SetValue(U&& value) {
        auto& state = RequireUnfulfilled();
        state.result.emplace(std::forward<U>(value));
        state.MarkReady();
    }

    /**
     * @brief Mark a Promise<void> fulfilled and wake the waiting fibers.
     * @throws std::logic_error if already fulfilled.
     */
    void SetValue()
        requires std::is_void_v<T>
    {
        RequireUnfulfilled().MarkReady();
    }

    /**
     * @brief Complete with an exception; Get()/Wait() rethrow it.
     * @throws std::logic_error if already fulfilled.
     */
    void SetException(std::exception_ptr ex) {
        auto& state = RequireUnfulfilled();
        state.exception = std::move(ex);
        state.MarkReady();
    }

    /// Whether SetValue() or SetException() was called (false once moved from).
    [[nodiscard]] bool IsFulfilled() const noexcept {
        return state_ && state_->ready;
    }

private:
    detail::FutureState<T>& RequireState() {
        if (!state_) {
            throw std::logic_error("Promise has no state (moved from)");
        }
        return *state_;
    }

    detail::FutureState<T>& RequireUnfulfilled() {
        auto& state = RequireState();
        if (state.ready) {
            throw std::logic_error("Promise already fulfilled");
        }
        return state;
    }

    void AbandonIfUnfulfilled() noexcept {
        if (state_ && !state_->ready) {
            state_->Abandon();
        }
    }

    std::shared_ptr<detail::FutureState<T>> state_;
    bool future_taken_ {false};
};

} // namespace cortex::tiny_fiber
