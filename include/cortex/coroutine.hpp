#pragma once

#include <cortex/config.hpp>

#if defined(CORTEX_EMSCRIPTEN)
#include <cortex/emscripten_coroutine.hpp>
#else
#include <cortex/native_coroutine.hpp>
#endif
