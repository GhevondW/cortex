#include <cortex/naive_coroutine.hpp>
#include <cortex/stack_allocator.hpp>

namespace cortex {

naive_coroutine::naive_coroutine(routine_t&& routine)
    : _completed(false)
    , _routine(std::move(routine))
    , _exe(execution::create_with_raw_flow(stack_allocator::create(1000000), this)) {}

naive_coroutine naive_coroutine::create(routine_t&& routine) {
    if (routine == nullptr) {
        throw error("Invalid routine.");
    }

    return naive_coroutine(std::move(routine));
}

std::unique_ptr<naive_coroutine> naive_coroutine::make(routine_t&& routine) {
    if (routine == nullptr) {
        throw error("Invalid routine.");
    }

    return std::unique_ptr<naive_coroutine> {new naive_coroutine(std::move(routine))};
}

void naive_coroutine::resume() {
    if (_completed) {
        throw resume_on_completed_coroutine("Logic error.");
    }

    try {
        _exe.resume();
    } catch (std::exception& exp) {
        _completed = true;
        throw;
    }
}

bool naive_coroutine::is_completed() const {
    return _completed;
}

void naive_coroutine::run(api::suspendable& suspender) {
    _routine(suspender);
    _completed = true;
}

} // namespace cortex
