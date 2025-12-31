#pragma once

#include <cortex/config.hpp>

#if defined(CORTEX_EMSCRIPTEN)
#include <cortex/detail/coroutine_emscripten.hpp>
#else
#include <cortex/detail/coroutine_native.hpp>
#endif

namespace cortex {
using Coroutine = detail::Coroutine;
};
