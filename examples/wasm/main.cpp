#include <cortex/core.hpp>
#include <iostream>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

int main() {
    std::cout << "Cortex WASM Example\n";
    std::cout << "Testing add function: 10 + 20 = " << cortex::add(10, 20) << "\n";
    
#ifdef __EMSCRIPTEN__
    std::cout << "Running in WASM environment\n";
    std::cout << "C API is exported and available via Module._cortex_add()\n";
#endif
    
    return 0;
}

