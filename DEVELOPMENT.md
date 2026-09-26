# Cortex - Development Guide

Stackful fibers for C++20 that keep the browser responsive — natively and in WebAssembly.

## Overview

Cortex is a C++ coroutine and fiber library that supports both native (Linux/macOS) and WebAssembly platforms. It uses:

- **Boost.Context** for native stackful coroutines (its ucontext backend in sanitizer builds)
- **Emscripten** for WebAssembly compilation
- **GoogleTest** for unit testing
- **CMake** with CPM for dependency management

## Prerequisites

### Using Docker (Recommended)

- Docker
- Docker Compose

### Local Development

- CMake 3.28+
- A C++20 compiler for the library; the repo's own tests, apps and examples use C++23 (Clang 19+ or GCC 13+)
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
- Build the Docker image (Ubuntu 24.04 with GCC 13, Clang 19 from Ubuntu's archive, and a pinned Emscripten)
- Configure CMake for native build (GCC 13, the image's default `c++`; `test-native-clang` builds the same tests with Clang 19)
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

## Development Workflows

### Making Code Changes

1. Edit source files in `src/`, `include/`, or `tests/`
2. Run tests to verify changes:
   ```bash
   # For native
   docker compose up test-native
   
   # For WASM
   docker compose up test-wasm
   ```

### Adding New Features

1. Add header declarations in `include/cortex/`
2. Implement in `src/`
3. Add tests in `tests/`
4. Update examples if needed

### Code Formatting

A format script is provided:

```bash
./format
```

This will format all C++ files according to the project style.

## Testing

### Running All Tests

```bash
# Native tests
docker compose up test-native

# WASM tests
docker compose up test-wasm
```

### Test Structure

Tests use GoogleTest. One executable per component lives in `tests/`:

```cpp
#include <cortex/tiny_fiber/tiny_fiber.hpp>

#include <gtest/gtest.h>

namespace tf = cortex::tiny_fiber;

TEST(TestSuiteName, TestName) {
    EXPECT_EQ(tf::Scheduler::Run([] { return 42; }), 42);
}
```

Timing-sensitive scheduler tests inject a fake clock through `Scheduler::Config::clock` (see `tests/tiny_fiber_timer_test.cpp`) instead of sleeping.

### Adding New Tests

1. Create `tests/<name>_test.cpp`.
2. Register it in `tests/CMakeLists.txt` with one line — `cortex_add_test(cortex_<name>_test <name>_test.cpp)` — which builds it natively (`gtest_discover_tests`) and for WASM (run with Node). Add `NATIVE_ONLY` or `WASM_ONLY` when a test only makes sense on one side.
3. Browser-level behaviour (the JS driver, `cortex::web::Await`) is tested end to end by `tests/web/*.mjs`, which load a real WebAssembly module in Node.
4. Run the native suite, the WASM suite (`./dev.sh test-wasm`), and ideally the sanitizer build.

## Examples

`-DCORTEX_BUILD_EXAMPLES=ON` builds the native examples in native builds and the browser demos in Emscripten builds.

### Native examples (`examples/linux/`)

- `runtime_tour.cpp` — a tour of `tiny_fiber`, one feature per section: time slicing with `CheckPoint()`, timeouts and cancellation, channels, `WaitAny`, `Promise` + `Post()` from a thread, a deadlock report, and a `RunFor()` event loop.
- `binary_tree.cpp` — tree traversal with a `Generator`.

```bash
docker compose up build-example-native   # builds both and runs them
```

### Browser demos (`examples/wasm/`)

- `responsive_demo.cpp` / `.html` — **Responsive C++**, the runtime showcase. A Mandelbrot explorer rendered by fibers that `js/cortex.mjs` drives, with a "plain call" mode for comparison. `responsive_demo_test.mjs` drives the same module in Node, so CI notices when the demo breaks.
- `fiber_workflow.cpp` / `fiber_demo.html` — producer and worker fibers over a bounded `Channel`.
- `sudoku_solver.cpp`, `particle_simulation.cpp`, `main.cpp` — raw `Coroutine`s resumed from JavaScript.

The demos that run fibers load `cortex.mjs` (copied next to them at build time) and call `drive(Module, scheduler)`. The build step copies `${cortex_JS_DRIVER}` to do that.

```bash
docker compose up build-example-wasm     # builds them; runs the Node smoke tests
docker compose up serve-example          # http://localhost:8080/examples/examples_index.html
```

## Building Locally

### Native Build

```bash
# Configure
cmake -B build/native -DCORTEX_BUILD_TESTS=ON -DCORTEX_BUILD_EXAMPLES=ON

# Build
cmake --build build/native

# Run tests
ctest --test-dir build/native

# Run the examples
./build/native/examples/runtime_tour
./build/native/examples/binary_tree_example
```

### WASM Build

First, set up Emscripten:

```bash
# Install emsdk
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install 4.0.23    # the version CI uses (EMSDK_VERSION in the Dockerfile)
./emsdk activate 4.0.23
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

- `CORTEX_BUILD_TESTS` - Build the library + per-component test binaries, including the native `apps/video_editor` engine tests (default: ON when cortex is the top-level project, OFF as a subproject)
- `CORTEX_BUILD_BENCHMARKS` - Build the micro-benchmarks in `benchmarks/` (default: OFF)
- `CORTEX_BUILD_EXAMPLES` - Build the examples in `examples/`: the native ones in native builds, the browser demos in Emscripten builds (default: OFF)
- `CORTEX_BUILD_APPS` - Build the full apps: `apps/algo_viz` and `apps/video_editor` (default: OFF)
- `CORTEX_BUILD_EXPERIMENTAL` - Build unfinished modules in `experimental/` (currently the `cortex::async` API design, all stubs) (default: OFF)
- `CORTEX_USE_SYSTEM_BOOST` - Use an installed Boost.Context (`find_package(Boost CONFIG COMPONENTS context)`) instead of fetching it (default: OFF)
- `CORTEX_INSTALL` - Generate install rules and the `cortex` CMake package (default: ON for top-level builds that can export: WASM, or native with system Boost)
- `CORTEX_WASM_ASYNCIFY_STACK_SIZE` - WASM only: bytes of Asyncify buffer per coroutine, bounding how deep a coroutine may be when it suspends (default: 65536; roughly 16–24 bytes per frame)
- `CORTEX_USE_SANITIZERS` - Enable Address and Undefined Behavior sanitizers (default: OFF). Works for both Native and WASM builds. Natively this switches the CPM-fetched Boost.Context to its ucontext backend, the only one that tells ASan about stack switches (with `CORTEX_USE_SYSTEM_BOOST=ON` the installed Boost must have been built that way). In WASM the Emscripten backend annotates its stack switches itself; sanitizer builds need more Asyncify buffer per frame.
- `CORTEX_ENABLE_LTO` - Enable link-time optimization for the cortex library (default: OFF)

When cortex is the top-level project and no `CMAKE_BUILD_TYPE` is given, the build defaults to `Release`. The `cortex::cortex` target requires C++20; the repo itself builds with C++23.

## Consuming Cortex (packaging checks)

Two scripts prove cortex works the way users consume it:

```bash
tests/package/check_subdirectory.sh   # add_subdirectory / FetchContent / CPM consumer, C++20
tests/package/check_install.sh        # system Boost → cmake --install → find_package(cortex)
```

Both build and run `tests/package/main.cpp`, and the first one also fails if cortex's tests, apps or GoogleTest leak into the consumer's build. Pass extra CMake arguments (for example `CPM_<Package>_SOURCE` overrides, to avoid downloads) through `CORTEX_PACKAGE_CMAKE_ARGS`. An installed cortex ships function2's header under `include/cortex/third_party`, so its package has no dependency beyond Boost.Context. It also installs the browser driver to `share/cortex/cortex.mjs`; `find_package(cortex)` sets `cortex_JS_DRIVER` to it (with `add_subdirectory`, the variable names `js/cortex.mjs` in the source tree). On Emscripten the package also exports `cortex::web`; point `find_package` at it with `-Dcortex_DIR=<prefix>/lib/cmake/cortex`, since the toolchain limits package lookup to its sysroot.

## Continuous Integration

`.github/workflows/ci.yml` runs on every pull request, each job in the Docker image:
- **Tests:**
  - native tests built with GCC 13 and with Clang 19;
  - WASM tests (Node);
  - sanitizers (ASan + UBSan, native and WASM);
  - the packaging checks;
  - clang-format.
- **Builds:** the native examples (then runs `runtime_tour`), the browser demos (then runs the Responsive C++ smoke test), and AlgoViz.
- **Benchmarks:** a comparison against the PR base (`benchmarks/ci_bench_check.sh`, 1.30× tolerance).

`nightly.yml` adds:
- a memory check: ASan + LeakSanitizer with stack-use-after-return detection. Valgrind cannot follow Boost.Context's stack switches;
- code coverage (lcov).

`deploy-demo.yml` publishes the demos and the Doxygen docs to GitHub Pages. The Emscripten version is pinned in the Dockerfile (`EMSDK_VERSION`) and in `deploy-demo.yml`; bump both together.

## Performance

- Fiber stacks are recycled: `tiny_fiber::Scheduler` uses a per-scheduler `cortex::PooledMemoryResource` by default (`tiny_fiber::MakeDefaultFiberResource()`), so `Spawn` reuses stacks, fiber objects and future state instead of hitting the system allocator. Pass your own `Scheduler::Config::memory_resource` to opt out or tune (`PooledMemoryResource::Config::max_cached_bytes` bounds the cache).
- On POSIX, the pooled stacks come from `cortex::MakeGuardedStackResource()`: each stack sits directly above a `PROT_NONE` page, so running off the end of a stack faults on the spot instead of corrupting memory (one guard page: a single frame larger than a page can jump it). Recycled stacks keep their guard page, so this costs nothing per `Spawn`.
- `tiny_fiber::CheckPoint()` costs a few nanoseconds when no yield is due (see the `checkpoint (no yield due)` benchmark): it reads the clock only every N calls, with N adapting to the loop's speed and capped at 32.
- The pool is intentionally not thread-safe; a scheduler and its fibers always live on one thread. For raw `Coroutine` use across threads, keep the default `GetDefaultMemoryResource()` or provide your own resource.

Run the micro-benchmarks to check hot-path regressions:

```bash
cmake -B build/native -DCMAKE_BUILD_TYPE=Release -DCORTEX_BUILD_BENCHMARKS=ON
cmake --build build/native --target cortex_bench
./build/native/benchmarks/cortex_bench            # optional: pass a name filter
```

## Building the Browser Demos

The demos are WASM apps; build them with Emscripten and `CORTEX_BUILD_APPS=ON`.

```bash
# AlgoViz
emcmake cmake -B build/wasm-algoviz -G Ninja -DCMAKE_BUILD_TYPE=Release -DCORTEX_BUILD_APPS=ON
cmake --build build/wasm-algoviz --config Release --target algo_viz

# Video Editor
emcmake cmake -B build/wasm-video-editor -G Ninja -DCMAKE_BUILD_TYPE=Release -DCORTEX_BUILD_APPS=ON
cmake --build build/wasm-video-editor --config Release --target video_editor
```

Serve either bundle with any static HTTP server (`python3 -m http.server 8080`) from its build directory, or use the helper script (`./dev.sh algoviz`, `./dev.sh video-editor`), which builds and serves in one step.

Release WASM app builds use `-O3` (see `cmake/AppRuntime.cmake`), and the video editor adds `-msimd128` for its per-pixel filter math (`apps/video_editor/CMakeLists.txt`). The cooperative renderer keeps the page responsive by running the real filters in a `tiny_fiber` fiber; they call `CheckPoint()` once per row and yield whenever their time slice is spent.

## Driving Fibers from JavaScript

`js/cortex.mjs` runs a `tiny_fiber::Scheduler` from the event loop.
- **What `drive()` does:** `drive(Module, scheduler, { budgetMs })` runs fibers in time-boxed slices, sleeps while they sleep, and is woken when a `Promise` or `Channel` is fed from JavaScript.
- **When `done` settles:** it resolves when every fiber finished, when `stop()` is called, or when the scheduler is destroyed (`~Scheduler` calls `Module.cortexForget`). It rejects on a deadlock, an escaped exception, or a trap or JavaScript exception inside a fiber. The last case also fails every other drive on the module.
- **How it talks to C++:** through the `cortex_scheduler_*` exports and the `cortex_js_*` hooks defined in `src/tiny_fiber/scheduler.cpp`.

`cortex::web::Await` (target `cortex::web`, embind) lets a fiber await any JavaScript promise. See the [guide](docs/guide.md#running-in-the-browser) for the reference, and the [porting guide](docs/porting-guide.md) for recipes.

## Platform Detection

The library automatically detects the build platform:

```cpp
#include <cortex/config.hpp>

#ifdef CORTEX_EMSCRIPTEN
    // WASM-specific code (defined, without a value, under Emscripten)
#else
    // Native-specific code
#endif
```

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

**Don't rely on the return value of an export that runs fibers.** Under Asyncify, switching fibers unwinds the stack back to the export's JavaScript wrapper, which records a placeholder return value and then rewinds and completes the call before returning to your code — so the work is done, but the returned value is the placeholder. Report results through a separate export that does not run fibers (as `editor_cooperative_done()` and `cortex_scheduler_last_status()` do) or through memory.

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

### `RuntimeError: unreachable` in a WASM build

**Problem:** A coroutine or fiber aborts with `RuntimeError: unreachable` while suspending.

**Solution:** Its call stack was too deep for the Asyncify buffer. Raise `CORTEX_WASM_ASYNCIFY_STACK_SIZE` (bytes per coroutine), or suspend at a shallower depth. Sanitizer builds need 2–3× more per frame.

### `drive()` rejects with "cannot switch to a coroutine: the Emscripten runtime has exited"

**Problem:** The module's runtime shut down after start-up (typically `main()` returned in a build with `EXIT_RUNTIME=1`, which `-fsanitize=address` turns on by default), so fibers cannot switch any more.

**Solution:** Link modules that JavaScript drives with `-sEXIT_RUNTIME=0`.

### `drive()` rejects with "this WebAssembly module can no longer run fibers"

**Problem:** A trap, or a JavaScript exception thrown by a synchronous `val` call, unwound a fiber's WebAssembly frames without running C++ destructors.

**Solution:** The first rejection names the original error. Fix that call: check values before converting them, or run code that may throw inside a promise and `Await()` it.

### A fiber crashes with SIGSEGV/SIGBUS

**Problem:** A native fiber dies with a segmentation fault or bus error.

**Solution:** Most likely it overflowed its stack and hit the guard page. Raise `Scheduler::Config::default_stack_size` (256 KB by default) or pass a stack size to `Spawn()`.

### A hang, or a `DeadlockError`

**Problem:** Fibers wait on each other forever.

**Solution:** Name fibers with `tiny_fiber::SetFiberName()`. The `DeadlockError` message (or `Scheduler::DescribeFibers()`) lists every live fiber and what it is waiting in. Don't suspend inside a `catch` block: exception state is per thread, and fibers share one.

### Linker Errors with Boost

**Problem:** Undefined references to Boost.Context

**Solution:** Boost is only used for native builds. Ensure you're not trying to link it in WASM builds. The CMake configuration handles this automatically.
