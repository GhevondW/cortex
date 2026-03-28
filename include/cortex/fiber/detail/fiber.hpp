#pragma once

#include <cortex/base_coroutine.hpp>
#include <cortex/coroutine_suspend_context.hpp>
#include <cortex/fiber/detail/platform.hpp>
#include <cortex/memory_resource.hpp>

#include <atomic>
#include <cstdint>
#include <memory>

#include <function2/function2.hpp>

namespace cortex::fiber::detail {

enum class FiberState : std::uint8_t {
    Ready,
    Running,
    Finished,
};

class Fiber final : public BaseCoroutine {
private:
    struct PrivateTag {};

public:
    using Id = std::uint64_t;
    using Body = fu2::unique_function<void()>;

    static std::unique_ptr<Fiber> Make(Body body, std::size_t stack_size, MemoryResourceSharedPtr resource);

    Fiber(PrivateTag, Id id, Body body, std::size_t stack_size, MemoryResourceSharedPtr resource);
    ~Fiber() override = default;

    Fiber(const Fiber&) = delete;
    Fiber& operator=(const Fiber&) = delete;
    Fiber(Fiber&&) = delete;
    Fiber& operator=(Fiber&&) = delete;

    [[nodiscard]] Id GetId() const noexcept {
        return id_;
    }

    [[nodiscard]] bool IsReady() const noexcept {
        return state_.load(std::memory_order_acquire) == FiberState::Ready;
    }

    [[nodiscard]] bool IsRunning() const noexcept {
        return state_.load(std::memory_order_acquire) == FiberState::Running;
    }

    [[nodiscard]] bool IsFinished() const noexcept {
        return state_.load(std::memory_order_acquire) == FiberState::Finished;
    }

    void Run();
    void Yield();
    void Complete() noexcept;

private:
    void Continuation(CoroutineSuspendContext& ctx) override;
    void Suspend();

private:
    Id id_;
    std::atomic<FiberState> state_ {FiberState::Ready};
    Body body_;
    CoroutineSuspendContext* suspend_ctx_ {nullptr};
};

} // namespace cortex::fiber::detail
