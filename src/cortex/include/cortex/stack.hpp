#ifndef SRC_CORTEX_INCLUDE_CORTEX_STACK_HPP
#define SRC_CORTEX_INCLUDE_CORTEX_STACK_HPP

#include <cstddef>

namespace cortex {

/**
 * @brief The `stack` class provides functionality for managing the stack of machine contexts.
 */
class stack {
public:
    /**
     * @brief Default constructor for the `stack` class.
     */
    stack() = default;

    /**
     * @brief Constructor to create a stack with a specified size and top pointer.
     *
     * @param size The size of the stack.
     * @param top The top pointer of the stack.
     */
    stack(std::size_t size, void* top)
        : _size(size)
        , _top(top) {}

    /**
     * @brief Default copy constructor for the `stack` class.
     */
    stack(const stack&) = default;

    /**
     * @brief Default move constructor for the `stack` class.
     */
    stack(stack&&) = default;

    /**
     * @brief Default copy assignment operator for the `stack` class.
     */
    stack& operator=(const stack&) = default;

    /**
     * @brief Default move assignment operator for the `stack` class.
     */
    stack& operator=(stack&&) = default;

    /**
     * @brief Returns the size of the stack.
     *
     * @return The size of the stack.
     */
    [[nodiscard]] inline std::size_t size() const noexcept;

    /**
     * @brief Returns the top pointer of the stack.
     *
     * @return The top pointer of the stack.
     */
    [[nodiscard]] inline void* top() const noexcept;

    /**
     * @brief Checks if the stack is empty.
     *
     * @return `true` if the stack is empty, `false` otherwise.
     */
    [[nodiscard]] inline bool empty() const noexcept;

    /**
     * @brief Releases the stack by resetting its size and top pointer.
     */
    inline void release() noexcept;

private:
    /// The size of the stack.
    std::size_t _size = 0;
    /// The top pointer of the stack.
    void* _top = nullptr;
};

/**
 * @brief Returns the size of the stack.
 *
 * @return The size of the stack.
 */
inline std::size_t stack::size() const noexcept {
    return _size;
}

/**
 * @brief Returns the top pointer of the stack.
 *
 * @return The top pointer of the stack.
 */
inline void* stack::top() const noexcept {
    return _top;
}

/**
 * @brief Checks if the stack is empty.
 *
 * @return `true` if the stack is empty, `false` otherwise.
 */
inline bool stack::empty() const noexcept {
    return _size == 0;
}

/**
 * @brief Releases the stack by resetting its size and top pointer.
 */
inline void stack::release() noexcept {
    _size = 0;
    _top = nullptr;
}

} // namespace cortex

#endif
