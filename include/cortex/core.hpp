#pragma once

#include <cortex/config.hpp>

namespace cortex {

[[nodiscard]] int add(int a, int b);

} // namespace cortex

extern "C" {

CORTEX_API int cortex_add(int a, int b);
}