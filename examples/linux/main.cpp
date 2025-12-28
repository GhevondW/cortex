#include <cortex/core.hpp>
#include <iostream>

int main() {
    std::cout << "Cortex Native Example\n";
    std::cout << "Testing add function: 5 + 7 = " << cortex::add(5, 7) << "\n";

    // Test the C API as well
    std::cout << "C API test: cortex_add(100, 200) = " << cortex_add(100, 200) << "\n";

    return 0;
}
