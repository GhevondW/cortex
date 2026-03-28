#pragma once

#include <cortex/config.hpp>

#ifdef CORTEX_EMSCRIPTEN
#error "cortex::fiber is not supported on WebAssembly. Use cortex::tiny_fiber."
#endif

