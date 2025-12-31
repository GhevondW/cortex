#pragma once

#include <function2/function2.hpp>

#include <cortex/coroutine_suspend_context.hpp>

namespace cortex {

// This is ok for now
using CoroutineBody = fu2::unique_function<void(CoroutineSuspendContext&)>;

} // namespace cortex
