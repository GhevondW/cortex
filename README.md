# Cortex Library

Cortex is a C++ library built on top of Boost.Context that provides a lightweight and flexible mechanism for managing execution flow and context in concurrent programming. It enables the creation of stackful coroutines, allowing you to write efficient and readable asynchronous code.

## Features

- **Execution Control:** Manage the flow of execution within contexts.
- **Context Management:** Create and control execution contexts.
- **Stack Allocation:** Customize stack allocation for execution contexts.
- **Forced Unwind:** Gracefully handle forced context unwinding.

## Installation

Clone the repository and build the library using CMake:

```bash
git clone https://gitlab.com/ghevond.98/cortex.git
cd cortex
mkdir build && cd build
cmake ..
make
```
## Usage

- **Include Cortex Headers** Include the necessary headers in your C++ code:
```c++
#include <cortex/execution.hpp>
#include <cortex/stack_allocator.hpp>
#include <cortex/basic_flow.hpp>
```

- **Create a Stackful Coroutine** Create a stackful coroutine with a custom stack allocator and a basic flow:
```c++
auto coroutine = cortex::execution::create(cortex::stack_allocator{1000000}, cortex::basic_flow::make([](cortex::api::disabler& dis) {
    // Your coroutine code here.
    // disable and jump back with dis.disable() if needed.
}));
```

- **Start the Coroutine** Start the coroutine to begin the asynchronous execution:
```c++
coroutine.enable();
```
