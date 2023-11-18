#include <cortex/error.hpp>
#include <cortex/stack_allocator.hpp>

#include <cassert>

namespace cortex {

stack_allocator stack_allocator::create(std::size_t size) {
    if (size == 0) {
        throw error("The input size is zero.");
    }
    return stack_allocator(size);
}

stack_allocator::stack_allocator(std::size_t size)
    : _size(size) {}

[[nodiscard]] stack stack_allocator::allocate() const {
    void* ptr = std::malloc(_size);
    if (ptr == nullptr) {
        throw std::bad_alloc();
    }

    void* top = static_cast<char*>(ptr) + _size;

    return stack(_size, top);
}

void stack_allocator::deallocate(stack& stack) const noexcept {
    assert(!stack.empty());
    assert(stack.top());
    assert(stack.size() == _size);

    std::free(static_cast<char*>(stack.top()) - stack.size());
}

} // namespace cortex
