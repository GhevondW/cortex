# Cortex

**C++23 stackful coroutines that keep the browser responsive.**

[![Live Demo](https://img.shields.io/badge/demo-live-brightgreen?style=for-the-badge)](https://GhevondW.github.io/cortex/)
[![Docs](https://img.shields.io/badge/docs-doxygen-blue?style=for-the-badge)](https://GhevondW.github.io/cortex/docs/)

Heavy C++ work — image filters, recursive search, simulation loops — normally blocks the browser's main thread and freezes the page. Cortex runs it as **stackful coroutines on a cooperative fiber scheduler** that the JS event loop drives **one step at a time**: the algorithm does a slice of work, yields, lets the browser repaint, and resumes. No Web Workers, no threads, no rewrite of your hot path.

## Live demos

No install — open in a browser:

- **[Video Editor](https://GhevondW.github.io/cortex/video-editor/index.html)** — open a local video and edit it **while it plays**: brightness, contrast, saturation and blur applied to every frame in C++/WASM. Turn on the Cortex cooperative engine, crank the blur, and the page never freezes.
- **[AlgoViz](https://GhevondW.github.io/cortex/algoviz/)** — interactive binary-search-tree and union-find algorithms.
- **[Sudoku Solver](https://GhevondW.github.io/cortex/examples/sudoku_demo.html)** — recursive backtracking, visualised live.
- **[Particle Simulation](https://GhevondW.github.io/cortex/examples/particle_demo.html)** — heavy compute vs. cooperative scheduling, side by side.
- **[All demos](https://GhevondW.github.io/cortex/)** · **[API docs](https://GhevondW.github.io/cortex/docs/)**

## What you get

| API | Purpose |
|---|---|
| `Coroutine` / `BaseCoroutine` / `Generator` | Stackful suspend/resume with a `MemoryResource` allocator hook. Boost.Context natively, Emscripten Asyncify under WASM. |
| `tiny_fiber::Scheduler` | Cooperative fiber scheduler with a step-based API: `Run()` natively, `Create()` + `Step()` to drive from `requestAnimationFrame`. |
| `tiny_fiber::Future<T>` / `Spawn` / `Yield` | Async results and explicit yield points; exceptions delivered via `Future::Get()`. |
| `tiny_fiber::Mutex` / `ConditionVariable` | Cooperative sync primitives — no OS threads, safe across `Stop()`. |

The same headers and sources compile to a native static library **and** a `.js` + `.wasm` bundle.

## Example

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

**Driving it from JS (WASM)** — create the scheduler once, then step it from `requestAnimationFrame`:

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

This is exactly how the Video Editor filters each frame in yielding row-bands, so even a heavy blur never freezes the page.

## Quick start

Docker is the only requirement:

```bash
./dev.sh test-all       # native + WASM tests
./dev.sh video-editor   # build & serve the Video Editor → http://localhost:8080
./dev.sh serve          # serve the example bundle      → http://localhost:8080
./dev.sh help           # all commands
```

Building without Docker, the full CMake option list, and IDE setup live in **[DEVELOPMENT.md](DEVELOPMENT.md)**.

## License

[MIT](LICENSE)
