#pragma once

#include <cortex/coroutine.hpp>
#include <cortex/coroutine_suspend_context.hpp>
#include <cortex/memory_resource.hpp>

#include <function2/function2.hpp>

#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

/**
 * @file generator.hpp
 * @brief Stackful generator built on top of Cortex coroutines.
 */

namespace cortex {

/**
 * @class Generator
 * @brief A stackful generator that yields values of type T.
 */
template <typename T>
class Generator final {
public:
    /**
     * @class YieldContext
     * @brief Context provided to the generator body to yield values.
     */
    class YieldContext {
    public:
        YieldContext(const YieldContext&) = delete;
        YieldContext(YieldContext&&) = delete;
        YieldContext& operator=(const YieldContext&) = delete;
        YieldContext& operator=(YieldContext&&) = delete;

        template <typename U>
        void Yield(U&& value) {
            state_->current = std::forward<U>(value);
            ctx_->Suspend();
        }

    private:
        struct State;
        friend class Generator;

        YieldContext(State* state, CoroutineSuspendContext* ctx) noexcept
            : state_(state)
            , ctx_(ctx) {}

        State* state_ {nullptr};
        CoroutineSuspendContext* ctx_ {nullptr};
    };

    using Body = fu2::unique_function<void(YieldContext&)>;

    /**
     * @brief Creates a new generator with the specified body and stack size.
     *
     * @param body The function or callable to execute within the generator.
     * @param stack_size_bytes The size of the stack to allocate for the coroutine (default: 256KB).
     * @param resource The memory resource to use for stack and implementation allocation (default:
     * GetDefaultMemoryResource()).
     * @return A Generator instance.
     * @throws std::invalid_argument if the body is empty or stack_size_bytes is 0 or resource is null.
     */
    static Generator Make(Body body,
                          std::size_t stack_size_bytes = 262144,
                          MemoryResourceSharedPtr resource = GetDefaultMemoryResource()) {
        if (!static_cast<bool>(body)) {
            throw std::invalid_argument("generator body is null.");
        }

        auto state = std::make_shared<State>();
        auto coroutine = Coroutine::Make(
            [state, body = std::move(body)](CoroutineSuspendContext& ctx) mutable {
                YieldContext yield_ctx(state.get(), &ctx);
                body(yield_ctx);
            },
            stack_size_bytes,
            std::move(resource));

        return Generator(std::move(coroutine), std::move(state));
    }

    /**
     * @struct Builder
     * @brief A builder class for creating Generator instances with custom configuration.
     */
    struct Builder {
    public:
        Builder()
            : stack_size_bytes_(262144)
            , memory_resource_(GetDefaultMemoryResource()) {}

        Generator Build(Body body) && {
            return Generator::Make(std::move(body), stack_size_bytes_, std::move(memory_resource_));
        }

        Builder SetStackSizeInBytes(std::size_t stack_size_bytes) && noexcept {
            stack_size_bytes_ = stack_size_bytes;
            return std::move(*this);
        }

        Builder SetMemoryResource(MemoryResourceSharedPtr resource) && noexcept {
            memory_resource_ = std::move(resource);
            return std::move(*this);
        }

    private:
        std::size_t stack_size_bytes_ {0};
        MemoryResourceSharedPtr memory_resource_ {nullptr};
    };

    Generator(const Generator&) = delete;
    Generator(Generator&&) noexcept = default;
    Generator& operator=(const Generator&) = delete;
    Generator& operator=(Generator&&) noexcept = default;
    ~Generator() = default;

    /**
     * @brief Checks if the generator has finished its execution.
     * @return true if execution is complete, false otherwise.
     */
    [[nodiscard]] bool IsDone() const noexcept {
        return coroutine_.IsDone();
    }

    /**
     * @brief Resumes execution until a value is yielded or the generator completes.
     * @return true if a value was yielded, false if the generator is done.
     */
    bool Next() {
        if (coroutine_.IsDone()) {
            return false;
        }

        state_->current.reset();

        coroutine_.Resume();
        return state_->current.has_value();
    }

    /**
     * @brief Moves out the current value and clears it.
     * @return The yielded value.
     * @throws std::logic_error if no value is available.
     */
    T DetachValue() {
        if (!state_->current.has_value()) {
            throw std::logic_error("generator has no value.");
        }

        T value = std::move(*state_->current);
        state_->current.reset();
        return value;
    }

private:
    struct State {
        std::optional<T> current;
    };

    explicit Generator(Coroutine coroutine, std::shared_ptr<State> state)
        : state_(std::move(state))
        , coroutine_(std::move(coroutine)) {}

    std::shared_ptr<State> state_;
    Coroutine coroutine_;
};

} // namespace cortex
