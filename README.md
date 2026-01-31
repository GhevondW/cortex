# Cortex

A C++ stackful coroutine library with WebAssembly support, featuring cooperative multitasking primitives.

## Features

- **Stackful Coroutines** - Full coroutine support with suspend/resume
- **tiny_fiber Module** - Cooperative multitasking scheduler with:
  - `Scheduler` - Fiber management with step-based API for WASM
  - `Future<T>` - Async result handling
  - `Mutex` / `ConditionVariable` - Cooperative synchronization (no OS threads!)
  - `Yield()` - Explicit context switching
- **WebAssembly Support** - Runs in browsers via Emscripten
- **Cross-Platform** - Native (Linux/macOS) and WASM from single codebase

## Live Demo

Try the interactive WASM examples in your browser (no installation required):

[![Live Demo](https://img.shields.io/badge/demo-live-brightgreen?style=for-the-badge)](https://GhevondW.github.io/cortex/)
[![Docs](https://img.shields.io/badge/docs-doxygen-blue?style=for-the-badge)](https://GhevondW.github.io/cortex/docs/)

- **[All Examples](https://GhevondW.github.io/cortex/)** - Browse all demos
- **[Fiber Workflow](https://GhevondW.github.io/cortex/examples/fiber_demo.html)** - Cooperative multitasking demo (New!)
- **[Sudoku Solver](https://GhevondW.github.io/cortex/examples/sudoku_demo.html)** - Recursive backtracking visualization (Must See!)
- **[Particle Simulation](https://GhevondW.github.io/cortex/examples/particle_demo.html)** - See coroutines in action! (Recommended)
- **[Basic Example](https://GhevondW.github.io/cortex/examples/index.html)** - Simple suspend/resume demo
- **[API Docs](https://GhevondW.github.io/cortex/docs/)** - Doxygen-generated documentation

## Quick Example

```cpp
#include <cortex/tiny_fiber/tiny_fiber.hpp>
namespace tf = cortex::tiny_fiber;

int main() {
    tf::Scheduler::Run([] {
        // Spawn a fiber that returns a value
        auto future = tf::Spawn([] {
            tf::Yield();  // Cooperative yield
            return 42;
        });
        
        // Do other work while fiber runs
        tf::Yield();
        
        // Get result (blocks until fiber completes)
        int result = future.Get();
    });
}
```

For WASM integration with JS event loop:
```cpp
auto scheduler = tf::Scheduler::Create([]{
    // Your fiber code
});

// Called from JS via setInterval
while (!scheduler.IsDone()) {
    scheduler.Step();  // Run one fiber until yield
}
```

## Quick Start

### Prerequisites

- Docker
- Docker Compose

### Using the Helper Script

```bash
./dev.sh test-all        # Run all tests
./dev.sh test-native     # Run native tests
./dev.sh test-wasm       # Run WASM tests
./dev.sh serve           # Serve WASM example in browser
./dev.sh help            # Show all commands
```

### Manual Commands

**Run Native Tests:**
```bash
docker compose up --build test-native
```

**Run WASM Tests:**
```bash
docker compose up --build test-wasm
```

### Run Examples

**Online (No Installation Required):**

Try the **[live demo](https://GhevondW.github.io/cortex/)** in your browser!

**Native (Local):**
```bash
docker compose up --build build-example-native
```

**WASM (Local Browser):**
```bash
docker compose up serve-example
# Open http://localhost:8080/examples/examples_index.html (all examples)
# Or http://localhost:8080/examples/sudoku_demo.html (sudoku solver - must see!)
# Or http://localhost:8080/examples/particle_demo.html (recommended interactive demo)
# Or http://localhost:8080/examples/index.html (basic example)
```

## Development

For detailed development instructions, see [DEVELOPMENT.md](DEVELOPMENT.md).

### IDE Setup

The project includes configurations for:
- **VSCode**: `.devcontainer/` for container development, `.vscode/` for tasks and settings
- **CLion**: Docker toolchain setup instructions in [DEVELOPMENT.md](DEVELOPMENT.md)

Quick start with VSCode:
1. Install "Remote - Containers" extension
2. Open project in VSCode
3. Click "Reopen in Container" when prompted
4. Start coding with full IntelliSense inside Docker!

## Building

### Native Build

```bash
cmake -B build/native -DCORTEX_BUILD_TESTS=ON
cmake --build build/native
ctest --test-dir build/native
```

### WASM Build

```bash
source /path/to/emsdk/emsdk_env.sh
emcmake cmake -B build/wasm -G Ninja -DCORTEX_BUILD_TESTS=ON
cmake --build build/wasm
ctest --test-dir build/wasm
```

## Testing

Both native and WASM builds include comprehensive test suites:

- **Native Tests**: Run with GoogleTest on Linux/macOS
- **WASM Tests**: Run with GoogleTest in Node.js

All tests must pass on both platforms before merging changes.

## Requirements

### Docker (Recommended)
- Docker
- Docker Compose

### Local Development
- CMake 3.25+
- C++23 compiler (Clang 19+ or GCC 13+)
- Ninja build system
- Emscripten SDK (for WASM)
- Node.js 18+ (for WASM tests)
