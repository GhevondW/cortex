/**
 * @file config.hpp
 * @brief Configuration and platform-specific macros for the cortex library.
 */

#pragma once

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
/**
 * @def CORTEX_EMSCRIPTEN
 * @brief Defined when building for the Emscripten platform.
 */
#define CORTEX_EMSCRIPTEN
/**
 * @def CORTEX_API
 * @brief Macro for exporting/preserving symbols in the C API.
 */
#define CORTEX_API EMSCRIPTEN_KEEPALIVE
#else
/**
 * @def CORTEX_API
 * @brief Macro for exporting symbols (no-op on native platforms).
 */
#define CORTEX_API
#endif
