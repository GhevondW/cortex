# Cortex - Development Guide

A stackful coroutine library with WebAssembly support.

## Overview

Cortex is a C++ coroutine library that supports both native (Linux/macOS/Windows) and WebAssembly platforms. It uses:

- **Boost.Context** for native stackful coroutines
- **Emscripten** for WebAssembly compilation
- **GoogleTest** for unit testing
- **CMake** with CPM for dependency management

## Prerequisites

### Using Docker (Recommended)

- Docker
- Docker Compose

### Local Development

- CMake 3.25+
- C++23 compatible compiler (Clang 19+ or GCC 13+)
- Ninja build system
- Emscripten SDK (for WASM builds)
- Node.js (for running WASM tests)

## Quick Start

### Using the Development Helper Script

A convenient `dev.sh` script is provided for common tasks:

```bash
./dev.sh test-all        # Run all tests (native + WASM)
./dev.sh test-native     # Run native tests only
./dev.sh test-wasm       # Run WASM tests only
./dev.sh example-native  # Build and run native example
./dev.sh example-wasm    # Build and run WASM example
./dev.sh serve           # Serve WASM example in browser
./dev.sh clean           # Clean all build artifacts
./dev.sh format          # Format all C++ code
./dev.sh shell           # Open shell in dev container
./dev.sh help            # Show all commands
```

### Manual Commands

You can also use Docker Compose directly:

#### 1. Run Native Tests

```bash
docker compose up --build test-native
```

This will:
- Build the Docker image with Clang 19
- Configure CMake for native build
- Compile the library and tests
- Run all unit tests

### 2. Run WASM Tests

```bash
docker compose up --build test-wasm
```

This will:
- Build with Emscripten toolchain
- Compile to WebAssembly
- Run tests in Node.js

### 3. Run Examples

**Native Example:**
```bash
docker compose up --build build-example-native
```

**WASM Example (CLI):**
```bash
docker compose up --build build-example-wasm
```

**WASM Example (Browser):**
```bash
docker compose up serve-example
```

Then open http://localhost:8080/examples/index.html in your browser.

## Building Locally

### Native Build

```bash
# Configure
cmake -B build/native -DCORTEX_BUILD_TESTS=ON -DCORTEX_BUILD_EXAMPLES=ON

# Build
cmake --build build/native

# Run tests
ctest --test-dir build/native

# Run example
./build/native/examples/native_example
```

### WASM Build

First, set up Emscripten:

```bash
# Install emsdk
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh
```

Then build:

```bash
# Configure
emcmake cmake -B build/wasm -G Ninja \
    -DCORTEX_BUILD_TESTS=ON \
    -DCORTEX_BUILD_EXAMPLES=ON

# Build
cmake --build build/wasm

# Run tests
ctest --test-dir build/wasm

# Run example
cd build/wasm/examples
node wasm_example.js

# Serve for browser
cd build/wasm
python3 -m http.server 8080
# Open http://localhost:8080/examples/index.html
```

## CMake Options

- `CORTEX_BUILD_TESTS` - Build unit tests (default: ON)
- `CORTEX_BUILD_EXAMPLES` - Build example applications (default: OFF)

## Exporting Functions to JavaScript

To export C++ functions to JavaScript, use the `CORTEX_API` macro:

```cpp
extern "C" {
    CORTEX_API int my_function(int arg) {
        return arg * 2;
    }
}
```

This expands to `EMSCRIPTEN_KEEPALIVE` on WASM builds and nothing on native builds.

## Working Inside Docker Container

You can develop directly inside the Docker container for a consistent environment.

### Opening an Interactive Shell

```bash
# Quick access using dev.sh
./dev.sh shell

# Or using docker compose directly
docker compose run --rm test-native bash
```

This will:
- Start a container with all development tools (Clang 19, CMake, Emscripten)
- Mount your workspace at `/workspace`
- Give you an interactive bash shell

### Development Inside Container

Once inside the container:

```bash
# You're now in /workspace (your project root)

# Configure native build
cmake -B build/native -DCORTEX_BUILD_TESTS=ON -DCORTEX_BUILD_EXAMPLES=ON

# Build
cmake --build build/native

# Run tests
ctest --test-dir build/native --output-on-failure

# Build WASM (inside container)
source /opt/emsdk/emsdk_env.sh
emcmake cmake -B build/wasm -G Ninja -DCORTEX_BUILD_TESTS=ON
cmake --build build/wasm
ctest --test-dir build/wasm --output-on-failure
```

### Iterative Development in Container

```bash
# 1. Open container shell
./dev.sh shell

# 2. Initial build
cmake -B build/native && cmake --build build/native

# 3. Make changes to source files (in your host editor)

# 4. Rebuild (in container shell)
cmake --build build/native

# 5. Run tests
ctest --test-dir build/native

# 6. Repeat steps 3-5 as needed
```

## IDE Setup

### VSCode Setup

This allows you to develop inside the Docker container with full IDE features.

**1. Install Extensions:**
```bash
code --install-extension ms-vscode-remote.remote-containers
code --install-extension ms-vscode.cpptools
code --install-extension ms-vscode.cmake-tools
```

**2. Create `.devcontainer/devcontainer.json`:**

```json
{
    "name": "Cortex Development",
    "dockerComposeFile": ["../docker-compose.yml"],
    "service": "test-native",
    "workspaceFolder": "/workspace",
    "customizations": {
        "vscode": {
            "extensions": [
                "ms-vscode.cpptools",
                "ms-vscode.cmake-tools",
                "twxs.cmake",
                "ms-vscode.cpptools-extension-pack"
            ],
            "settings": {
                "cmake.configureOnOpen": false,
                "C_Cpp.default.compilerPath": "/usr/bin/clang++",
                "C_Cpp.default.cppStandard": "c++23",
                "C_Cpp.default.intelliSenseMode": "linux-clang-x64"
            }
        }
    },
    "postCreateCommand": "cmake -B build/native -DCORTEX_BUILD_TESTS=ON",
    "remoteUser": "root"
}
```

**3. Open in Container:**
- Press `F1` or `Cmd+Shift+P`
- Type "Remote-Containers: Reopen in Container"
- VSCode will rebuild and connect to the container

**4. Configure CMake in VSCode:**
- Press `Ctrl+Shift+P` (or `Cmd+Shift+P` on Mac)
- Type "CMake: Configure"
- Select "Clang 19" as compiler
- Build target: Press `F7` or use CMake sidebar

### CLion Setup

**1. Configure Docker Toolchain:**

1. Open **Settings** → **Build, Execution, Deployment** → **Toolchains**
2. Click **+** and select **Docker**
3. Configure:
   - **Name:** `Cortex Docker`
   - **Image:** `cortex-test-native` (or build it first with `docker compose build test-native`)
   - **CMake:** Detected automatically inside container

**2. Configure CMake Profile:**

1. Go to **Settings** → **Build, Execution, Deployment** → **CMake**
2. Add new profile:
   - **Name:** `Docker-Debug`
   - **Build type:** `Debug`
   - **Toolchain:** `Cortex Docker`
   - **CMake options:** `-DCORTEX_BUILD_TESTS=ON -DCORTEX_BUILD_EXAMPLES=ON`
   - **Build directory:** `build/native`

**3. Configure File Mappings:**

1. In **Toolchains** settings
2. Add volume mapping:
   - **Local path:** `/path/to/cortex` (your project root)
   - **Container path:** `/workspace`

**4. Build and Run:**
- Use CLion's build button (Ctrl+F9)
- Run tests from CMake panel
- All compilation happens inside Docker

### Recommended Workflow

**For VSCode Users:**
1. Get IntelliSense, debugging, and building all in container
2. No local compiler setup needed

**For CLion Users:**
1. Use Docker Toolchain for seamless integration
2. All builds happen in Docker automatically
3. Full IDE features with consistent environment

**For Quick Edits:**
1. Edit files locally with any editor
2. Run `./dev.sh test-all` to verify
3. Use `./dev.sh shell` for container access when needed

## Troubleshooting

### Docker Build Issues

**Problem:** Docker build fails with permission errors

**Solution:** Ensure Docker daemon is running and you have proper permissions:
```bash
docker ps  # Test Docker access
```

### CMake Configuration Issues

**Problem:** CMake can't find dependencies

**Solution:** Clean the build directory and reconfigure:
```bash
rm -rf build/native build/wasm
docker compose up test-native  # Will rebuild from scratch
```

### WASM Tests Fail

**Problem:** Node.js can't run WASM tests

**Solution:** Ensure you're using a recent Node.js version (18+). The Docker image includes the correct version.

### Build Artifacts Persist

**Problem:** Old build artifacts cause issues

**Solution:** Clean build directories:
```bash
rm -rf build/
docker compose down  # Clean Docker containers
```

### Linker Errors with Boost

**Problem:** Undefined references to Boost.Context

**Solution:** Boost is only used for native builds. Ensure you're not trying to link it in WASM builds. The CMake configuration handles this automatically.
