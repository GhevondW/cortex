#include <cortex/execution.hpp>
#include <cortex/stack_allocator.hpp>
#include <iostream>
#include <cortex/basic_flow.hpp>

namespace {}

int main() {
    auto execution = cortex::execution::create(cortex::stack_allocator {1000000}, cortex::basic_flow::make( [](cortex::api::disabler& dis) {
        std::cout << "Hello 1" << '\n';
        std::cout << "Hello 2" << '\n';
        dis.disable();
        std::cout << "Hello 3" << '\n';
        std::cout << "Hello 4" << '\n';
    }));

    execution.enable();

    std::cout << "Main end" << '\n';

//    execution.enable();

    return 0;
}
