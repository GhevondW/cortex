#include <cortex/coroutine.hpp>

#include <cassert>

namespace cortex {

coroutine::coroutine(cortex::stack_allocator alloc, routine_i* routine)
    : _completed(false)
    , _routine(routine)
    , _suspender(nullptr)
    , _exec(execution::create_with_raw_flow(std::move(alloc), this)) {}

coroutine coroutine::create(cortex::stack_allocator&& alloc, routine_i* routine) {
    if (routine == nullptr) {
        throw invalid_argument_error("The input routine is nullptr.");
    }

    return coroutine(std::move(alloc), routine);
}

coroutine coroutine::create(routine_i* routine) {
    return create(stack_allocator::create(1024 * 1024), routine);
}

std::unique_ptr<coroutine::basic_routine> coroutine::make_routine(std::function<void()>&& func) {
    return basic_routine::make(std::move(func));
}

void coroutine::resume() {
    if (_completed) {
        throw resume_on_completed_coroutine("The coroutine is completed.");
    }

    try {
        _exec.resume();
    } catch (std::exception& exp) {
        _completed = true;
        throw;
    }
}

void coroutine::suspend() {
    if (_suspender == nullptr) {
        throw suspend_on_not_started_coroutine("Unable to suspend not started coroutine.");
    }

    _suspender->suspend();
}

bool coroutine::is_completed() const {
    return _completed;
}

void coroutine::run(api::suspendable& suspender) {
    _suspender = &suspender;
    _routine->run_routine();
    _completed = true;
}

coroutine::basic_routine::basic_routine(std::function<void()>&& func)
    : _func(std::move(func)) {}

std::unique_ptr<coroutine::basic_routine> coroutine::basic_routine::make(std::function<void()>&& func) {
    if (func == nullptr) {
        throw invalid_argument_error("The input func is nullptr.");
    }

    return std::unique_ptr<basic_routine>(new basic_routine(std::move(func)));
}

void coroutine::basic_routine::run_routine() {
    _func();
}

} // namespace cortex
