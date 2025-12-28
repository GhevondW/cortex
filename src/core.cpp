#include "cortex/core.hpp"

namespace cortex {

int add(int a, int b) {
    return a + b;
}

} // namespace cortex

extern "C" {

CORTEX_API int cortex_add(int a, int b) {
    return cortex::add(a, b);
}
}