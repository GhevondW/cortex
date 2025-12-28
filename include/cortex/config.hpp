#pragma once

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define CORTEX_EMSCRIPTEN 1
#define CORTEX_API EMSCRIPTEN_KEEPALIVE
#else
#define CORTEX_EMSCRIPTEN 0
#define CORTEX_API
#endif
