# Cortex

**A C++23 stackful coroutine library that keeps the UI responsive — in the browser, via WebAssembly.**

[![Live Demo](https://img.shields.io/badge/demo-live-brightgreen?style=for-the-badge)](https://GhevondW.github.io/cortex/)
[![Docs](https://img.shields.io/badge/docs-doxygen-blue?style=for-the-badge)](https://GhevondW.github.io/cortex/docs/)

Heavy C++ work (image filters, recursive search, simulation loops) normally blocks the browser's main thread and freezes the page. Cortex gives you **stackful coroutines + a cooperative fiber scheduler** that the JS event loop can drive **one step at a time**, so the same algorithm finishes a frame, yields, lets the browser repaint, and resumes — no Web Workers, no threads, no rewrites of your hot path.

## Live Demos

Try them in your browser, no installation required.

- **[Video Editor](https://GhevondW.github.io/cortex/video-editor/index.html)** — apply a Gaussian-blur pipeline to every frame; toggle between *blocking* (page freezes) and *cooperative* (UI stays smooth). The "smoking gun" spinner proves it. **New, recommended.**
- **[AlgoViz — BST Visualizer](https://GhevondW.github.io/cortex/algoviz/)** — interactive binary-search-tree algorithms.
- **[Sudoku Solver](https://GhevondW.github.io/cortex/examples/sudoku_demo.html)** — recursive backtracking visualised live.
- **[Particle Simulation](https://GhevondW.github.io/cortex/examples/particle_demo.html)** — heavy compute vs. cooperative scheduling, side by side.
- **[Fiber Workflow](https://GhevondW.github.io/cortex/examples/fiber_demo.html)** — producer / worker pattern with `tiny_fiber`.
- **[Basic Example](https://GhevondW.github.io/cortex/examples/index.html)** — minimal suspend / resume.
- **[All Demos](https://GhevondW.github.io/cortex/)** · **[API Docs](https://GhevondW.github.io/cortex/docs/)**

## What you get

| | |
|---|---|
| `Coroutine` / `BaseCoroutine` / `Generator` | Stackful suspend / resume with a `MemoryResource` abstraction for custom allocators. Backed by **Boost.Context** natively and **Emscripten Asyncify** under WASM. |
| `tiny_fiber::Scheduler` | Cooperative fiber scheduler with a **step-based API** — drive it from `requestAnimationFrame` and the page never freezes. `Run()` for native, `Create()` + `Step()` for WASM. |
| `tiny_fiber::Future<T>` / `Spawn` / `Yield` | Async result handling plus explicit yield points; deliver exceptions via `Future::Get()`. |
| `tiny_fiber::Mutex` / `ConditionVariable` | Cooperative synchronisation primitives — no OS threads, robust against `Stop()` mid-wait. |
| **Cross-platform from one codebase** | Same headers / sources compile to a native static library *and* to a `.js` + `.wasm` bundle. |

## Quick Example

```cpp
#include <cortex/tiny_fiber/tiny_fiber.hpp>
namespace tf = cortex::tiny_fiber;

int main() {
    tf::Scheduler::Run([] {
        // Spawn parallel fibers, each yielding cooperatively.
        auto a = tf::Spawn([] { tf::Yield(); return 6; });
        auto b = tf::Spawn([] { tf::Yield(); return 7; });

        // Get() blocks the current fiber (not the OS thread) until ready.
        int result = a.Get() * b.Get();
        std::printf("Result: %d\n", result);
    });
}
```

### Driving the scheduler from the JS event loop (WASM)

```cpp
auto scheduler = tf::Scheduler::Create([]{
    for (int i = 0; i < total_frames; ++i) {
        process_frame(i);
        tf::Yield();   // let the browser breathe between frames
    }
});

// From JS, called once per requestAnimationFrame:
//   while (Module._step()) {}
extern "C" int step() { return scheduler->Step() ? 1 : 0; }
```

This is the exact pattern used by the [Video Editor demo](https://GhevondW.github.io/cortex/video-editor/index.html) — the cooperative "apply filter to all frames" path stays responsive while the blocking variant locks up the page.

## Quick Start

The simplest path is Docker; everything below assumes Docker + Docker Compose are installed.

### Helper script

```bash
./dev.sh test-all         # Run all native + WASM tests
./dev.sh test-native      # Native tests only
./dev.sh test-wasm        # WASM tests in Node.js
./dev.sh serve            # Serve the WASM example bundle  → http://localhost:8080
./dev.sh algoviz          # Build & serve AlgoViz          → http://localhost:8080
./dev.sh video-editor     # Build & serve the Video Editor → http://localhost:8080
./dev.sh format           # clang-format the C++ tree
./dev.sh shell            # Open a shell in the dev container
./dev.sh help             # All commands
```

### Or use Docker Compose directly

```bash
docker compose up --build test-native        # native test run
docker compose up --build test-wasm          # WASM test run (Node)
docker compose up serve-example              # http://localhost:8080
docker compose up --build serve-algoviz      # http://localhost:8080
docker compose up --build serve-video-editor # http://localhost:8080
```

## Building locally (no Docker)

### Native (Linux / macOS)

```bash
cmake -B build/native -DCORTEX_BUILD_TESTS=ON
cmake --build build/native
ctest --test-dir build/native
```

### WebAssembly

```bash
source /path/to/emsdk/emsdk_env.sh
emcmake cmake -B build/wasm -G Ninja -DCORTEX_BUILD_TESTS=ON
cmake --build build/wasm
ctest --test-dir build/wasm
```

### Build the browser demos

```bash
# AlgoViz
emcmake cmake -B build/wasm-algoviz -G Ninja -DCORTEX_BUILD_APPS=ON
cmake --build build/wasm-algoviz --target algo_viz

# Video Editor
emcmake cmake -B build/wasm-video-editor -G Ninja -DCORTEX_BUILD_APPS=ON
cmake --build build/wasm-video-editor --target video_editor
```

Serve either bundle via any static HTTP server (`python3 -m http.server 8080`) from its build directory.

### CMake options

| Option | Default | Description |
|---|---|---|
| `CORTEX_BUILD_TESTS` | `ON` | Build the library + per-component test binaries (also builds the engine tests under `apps/video_editor`). |
| `CORTEX_BUILD_EXAMPLES` | `OFF` | Build the standalone examples in `examples/`. |
| `CORTEX_BUILD_APPS` | `OFF` | Build the full apps (`apps/algo_viz`, `apps/video_editor`). |
| `CORTEX_USE_SANITIZERS` | `OFF` | Enable ASan + UBSan (and `BOOST_USE_ASAN` for Boost.Context). |

## Project layout

```
include/cortex/             public headers (Coroutine, Generator, tiny_fiber/*)
src/                        library implementation + per-platform PIMPL (Boost / Emscripten)
tests/                      GoogleTest binaries for every library component
examples/                   small WASM demos (one .cpp each + an HTML driver)
apps/
  algo_viz/                 interactive algorithm visualizer (BST, Union-Find)
  video_editor/             video-filter demo with cooperative vs blocking comparison
    engine/                 layered engine library: filters, pipeline, runners (testable natively)
    web/                    static HTML + modular JS (canvas, timeline, controls, …)
cmake/                      build helpers, including the shared cortex_add_wasm_app_runtime()
```

## Requirements

**With Docker** (recommended): only Docker and Docker Compose.

**Without Docker:**
- CMake 3.28+
- C++23 compiler — Clang 19+ or GCC 13+
- Ninja
- Emscripten SDK 3.x (for WASM)
- Node.js 18+ (to run WASM tests)

## Development

See [DEVELOPMENT.md](DEVELOPMENT.md) for the longer walkthrough, IDE setup (VSCode dev containers, CLion Docker toolchain), and the iterative-edit workflow.

## License

[MIT](LICENSE)
