#pragma once

#include <cortex/config.hpp>

#if defined(CORTEX_EMSCRIPTEN)
#include <cortex/coroutine_emscripten.hpp>
#else
#include <cortex/coroutine_native.hpp>
#endif
