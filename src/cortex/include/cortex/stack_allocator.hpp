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
public:
    /**
     * @brief Explicit constructor to create a `stack_allocator` with a specified stack size.
     *
     * @param size The size of the stacks to be allocated by this allocator.
     */
    explicit stack_allocator(std::size_t size);

    /**
     * @brief Default destructor for the `stack_allocator` class.
     */
    ~stack_allocator() noexcept = default;

    /**
     * @brief Allocates a new stack with the configured size.
     *
     * @return A new stack with the specified size.
     */
    [[nodiscard]] stack allocate() const;

    /**
     * @brief Deallocates a previously allocated stack.
     *
     * @param stack The stack to deallocate.
     */
    void deallocate(stack& stack) const noexcept;

private:
    /// The size of the stacks to be allocated by this allocator.
    const std::size_t _size;
};

} // namespace cortex

#endif
