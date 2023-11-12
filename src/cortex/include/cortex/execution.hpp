#ifndef SRC_CORTEX_INCLUDE_CORTEX_CONTEXT_HPP
#define SRC_CORTEX_INCLUDE_CORTEX_CONTEXT_HPP

#include <cortex/api/disabler.hpp>
#include <cortex/api/flow.hpp>
#include <cortex/error.hpp>
#include <cortex/machine_context.hpp>
#include <cortex/stack.hpp>

#include <cassert>
#include <iostream>
#include <memory>
#include <type_traits>

namespace cortex {

/**
 * @brief The `forced_unwind` struct represents an exception used for forced context unwinding.
 */
struct forced_unwind : public error {
    machine::context_t context {nullptr};

    explicit forced_unwind(machine::context_t ctx)
        : error(std::string())
        , context(ctx) {}
};

template <typename Alloc>
inline static constexpr bool
    is_deallocate_noexcept_v = noexcept(std::declval<std::decay_t<Alloc>>().deallocate(std::declval<stack&>()));

/**
 * @brief The `disabler` class provides a mechanism for disabling the execution flow of a context.
 */
struct disabler : public api::disabler {
    explicit disabler(machine::transfer_t& t, machine::context_t c)
        : transfer(t)
        , caller(c) {}

    disabler(const disabler&) = delete;
    disabler(disabler&&) = delete;
    disabler& operator=(const disabler&) = delete;
    disabler& operator=(disabler&&) = delete;

    ~disabler() override = default;

    void disable() override {
        transfer = machine::jump_to_context(caller, nullptr);
    }

private:
    machine::transfer_t& transfer;
    machine::context_t caller;
};

/**
 * @brief The `execution` class provides control over the execution flow and context management.
 */
class execution {
private:
    template <typename StackAlloc>
    class frame {
        friend class execution;
        using stack_allocator_t = std::decay_t<StackAlloc>;

        static void entry(machine::transfer_t transfer) noexcept;
        static machine::transfer_t exit(machine::transfer_t transfer) noexcept;

    public:
        frame(stack_allocator_t&& alloc, stack st, std::unique_ptr<api::flow> flow);
        frame() = default;

        void run(api::disabler& dis);

        void destroy();

    private:
        stack_allocator_t _allocator;
        stack _stack;
        std::unique_ptr<api::flow> _flow;
    };

public:
    /**
     * @brief The `invalid_flow` class represents an exception for an invalid execution flow.
     */
    class invalid_flow : public error {
    public:
        using error::error;
    };

    /**
     * @brief The `invalid_flow` class represents an exception for an invalid stack size.
     */
    class invalid_stack_size : public error {
    public:
        using error::error;
    };

    /**
     * @brief Default constructor for the `execution` class.
     */
    execution() = default;

    execution(const execution&) = delete;
    execution(execution&&) = delete;
    execution& operator=(const execution&) = delete;
    execution& operator=(execution&&) = delete;

    /**
     * @brief Creates a new `execution` with the specified stack allocator and execution flow.
     *
     * @tparam StackAlloc The type of the stack allocator.
     * @param alloc The stack allocator instance.
     * @param flow The execution flow to be associated with the execution.
     * @return A new `execution` instance.
     * @throws invalid_flow if the input flow is nullptr.
     */
    template <typename StackAlloc>
    static execution create(StackAlloc&& alloc, std::unique_ptr<api::flow> flow);

    /**
     * @brief Destructor for the `execution` class.
     */
    ~execution() noexcept;

    /**
     * @brief Enables the execution flow of the context.
     */
    void enable();

private:
    /**
     * @brief Private constructor for creating an `execution` with the specified machine context.
     *
     * @param context The machine context associated with the execution.
     */
    explicit execution(machine::context_t context);

    /// The machine context associated with the execution.
    machine::context_t _context = nullptr;
};

template <typename StackAlloc>
void execution::frame<StackAlloc>::entry(machine::transfer_t transfer) noexcept {
    // transfer control structure to the context-stack
    auto fr = static_cast<frame*>(transfer.data);
    assert(nullptr != transfer.fctx);
    assert(nullptr != fr);
    try {
        // jump back to `create_context()`
        transfer = machine::jump_to_context(transfer.fctx, nullptr);
        // start executing
        disabler dis(transfer, transfer.fctx);
        fr->run(dis);
    } catch (const forced_unwind& ex) {
        transfer = {ex.context, nullptr};
    }
    assert(nullptr != transfer.fctx);
    // destroy context-stack of `this`context on next context
    [[maybe_unused]] auto res = machine::ontop_context(transfer.fctx, fr, &exit);
    assert(false); // context already terminated
}

template <typename StackAlloc>
machine::transfer_t execution::frame<StackAlloc>::exit(machine::transfer_t transfer) noexcept {
    auto fr = static_cast<frame*>(transfer.data);
    // destroy context stack
    fr->destroy();
    return {nullptr, nullptr};
}

template <typename StackAlloc>
execution::frame<StackAlloc>::frame(stack_allocator_t&& alloc, stack st, std::unique_ptr<api::flow> flow)
    : _allocator(std::move(alloc))
    , _stack(st)
    , _flow(std::move(flow)) {}

template <typename StackAlloc>
void execution::frame<StackAlloc>::run(api::disabler& dis) {
    assert(_flow);
    _flow->run(dis);
}

template <typename StackAlloc>
void execution::frame<StackAlloc>::destroy() {
    this->~frame();
    _allocator.deallocate(_stack);
}

template <typename StackAlloc>
execution execution::create(StackAlloc&& alloc, std::unique_ptr<api::flow> flow) {
    static_assert(is_deallocate_noexcept_v<StackAlloc>);

    if (flow == nullptr) {
        throw execution::invalid_flow("The input flow is nullptr.");
    }

    auto stack = alloc.allocate();
    using frame_t = frame<StackAlloc>;

    static constexpr std::size_t min_stack_size = 128000; // 128 KB
    if (stack.size() < min_stack_size) {
        alloc.deallocate(stack);
        throw invalid_stack_size("The allocated stack size is small, must be 128 KB min.");
    }

    // reserve space for control structure
    void* storage =
        reinterpret_cast<void*>((reinterpret_cast<uintptr_t>(stack.top()) - static_cast<uintptr_t>(sizeof(frame_t))) &
                                ~static_cast<uintptr_t>(0xff));
    // placment new for control structure on context stack
    [[maybe_unused]] frame_t* fr = new (storage) frame_t {std::forward<StackAlloc>(alloc), stack, std::move(flow)};
    // 64byte gab between control structure and stack top
    // should be 16byte aligned
    void* stack_top = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(storage) - static_cast<uintptr_t>(64));
    void* stack_bottom =
        reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(stack.top()) - static_cast<uintptr_t>(stack.size()));
    // create fast-context
    const std::size_t size = reinterpret_cast<uintptr_t>(stack_top) - reinterpret_cast<uintptr_t>(stack_bottom);

    const machine::context_t ctx = machine::make_context(stack_top, size, &frame_t::entry);
    assert(nullptr != ctx);
    // transfer control structure to context-stack
    return execution(machine::jump_to_context(ctx, fr).fctx);
}

} // namespace cortex

#endif
