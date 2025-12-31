#pragma once

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define CORTEX_EMSCRIPTEN
#define CORTEX_API EMSCRIPTEN_KEEPALIVE
#else
#define CORTEX_API
#endif
