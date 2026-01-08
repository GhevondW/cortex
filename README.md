# Cortex

A C++ stackful coroutine library with WebAssembly support.

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

**Native:**
```bash
docker compose up --build build-example-native
```

**WASM (Browser):**
```bash
docker compose up serve-example
# Open http://localhost:8080/examples/examples_index.html (all examples)
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
