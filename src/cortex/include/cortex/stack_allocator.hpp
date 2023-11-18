#ifndef SRC_CORTEX_INCLUDE_CORTEX_STACK_ALLOCATOR_HPP
#define SRC_CORTEX_INCLUDE_CORTEX_STACK_ALLOCATOR_HPP

#include <cortex/stack.hpp>

namespace cortex {

/**
 * @brief The `stack_allocator` class provides functionality for allocating and deallocating stacks for machine
 * contexts.
 *
 * @note Users can provide their own stack allocator by implementing the following named requirements:
 * - `stack allocate() const`: Allocates a new stack with the configured size.
 * - `void deallocate(stack& stack) const noexcept`: Deallocates a previously allocated stack.
 */
class stack_allocator {
private:
    /**
     * @brief Private constructor to enforce the use of the factory function `create`.
     *
     * @param size The size of the stacks to be allocated by this allocator.
     */
    explicit stack_allocator(std::size_t size);

public:
    /**
     * @brief Factory function to create a `stack_allocator` with the specified size.
     *
     * @param size The size of the stacks to be allocated.
     * @return A new instance of `stack_allocator`.
     * @throws cortex::error if the input size is zero.
     */
    static stack_allocator create(std::size_t size);

    /**
     * @brief Default destructor for the `stack_allocator` class.
     */
    ~stack_allocator() noexcept = default;

    /**
     * @brief Allocates a new stack with the configured size.
     *
     * @return A new stack with the specified size.
     * @throws std::bad_alloc if memory allocation fails.
     */
    [[nodiscard]] stack allocate() const;

    /**
     * @brief Deallocates a previously allocated stack.
     *
     * @param stack The stack to deallocate.
     * @throws std::logic_error if the stack is empty or has an invalid size/top.
     */
    void deallocate(stack& stack) const noexcept;

private:
    /// The size of the stacks to be allocated by this allocator.
    const std::size_t _size;
};

} // namespace cortex

#endif
