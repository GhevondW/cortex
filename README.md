# Cortex

**Make C++ behave in the browser.** Stackful fibers for C++20 that share the main thread fairly, sleep, cancel, and await JavaScript promises — so heavy C++ never freezes the page. The same code runs natively.

[![Live Demo](https://img.shields.io/badge/demo-live-brightgreen?style=for-the-badge)](https://GhevondW.github.io/cortex/)
[![Docs](https://img.shields.io/badge/docs-doxygen-blue?style=for-the-badge)](https://GhevondW.github.io/cortex/docs/)

Compiled to WebAssembly, a long C++ call runs on the browser's main thread and the page stops painting and answering input until it returns. Cortex runs that code in **fibers**: plain, synchronous-looking C++ that pauses at cheap checkpoints whenever it has used its time slice, lets the browser breathe, and carries on. Fibers can also wait — for a timer, for another fiber, for a JavaScript `Promise` — without blocking anything else. No Web Workers, no `SharedArrayBuffer` headers, no rewrite into callbacks.

## Live demos

No install — open in a browser:

- **[Responsive C++](https://GhevondW.github.io/cortex/examples/responsive_demo.html)** — a Mandelbrot explorer rendered by C++ fibers. Zoom, type and watch the pulse while it renders; clicks cancel stale renders. Flip to "plain call" to run the very same C++ directly and watch the page freeze.
- **[Video Editor](https://GhevondW.github.io/cortex/video-editor/index.html)** — open a local video and edit it **while it plays**: brightness, contrast, saturation and blur applied to every frame in C++/WASM. Turn on the Cortex cooperative engine, crank the blur, and the page never freezes.
- **[AlgoViz](https://GhevondW.github.io/cortex/algoviz/)** — interactive binary-search-tree and union-find algorithms.
- **[Sudoku Solver](https://GhevondW.github.io/cortex/examples/sudoku_demo.html)** — recursive backtracking, visualised live.
- **[Particle Simulation](https://GhevondW.github.io/cortex/examples/particle_demo.html)** — the same simulation run blocking, then as a coroutine resumed once per frame.
- **[Fiber Workflow](https://GhevondW.github.io/cortex/examples/fiber_demo.html)** — a producer and worker fibers over a bounded channel.
- **[All demos](https://GhevondW.github.io/cortex/)** · **[API docs](https://GhevondW.github.io/cortex/docs/)**

## In 30 seconds

```cpp
#include <cortex/tiny_fiber/tiny_fiber.hpp>

#include <chrono>
#include <cstdio>

namespace tf = cortex::tiny_fiber;
using namespace std::chrono_literals;

// Ordinary C++. Called directly it simply runs; inside a fiber, CheckPoint()
// yields whenever this fiber has used up its time slice (a few ns otherwise).
long CountPrimes(int limit) {
    long count = 0;
    for (int n = 2; n < limit; ++n) {
        bool prime = true;
        for (int d = 2; d * d <= n && prime; ++d) {
            prime = n % d != 0;
        }
        count += prime ? 1 : 0;
        tf::CheckPoint();
    }
    return count;
}

int main() {
    const long primes = tf::Scheduler::Run([] {
        auto work = tf::Spawn([] { return CountPrimes(2'000'000); });

        // Runs interleaved with the computation above.
        auto ticker = tf::Spawn([] {
            for (int i = 0; i < 3; ++i) {
                std::puts("still responsive");
                tf::SleepFor(10ms);
            }
        });

        if (!work.WaitFor(5s)) {
            work.Cancel(); // Get() below then throws tf::CancelledError
        }
        return work.Get();
    });
    std::printf("%ld primes\n", primes);
}
```

### In the browser

Run fibers from the JavaScript event loop with the driver in [`js/cortex.mjs`](https://github.com/GhevondW/cortex/blob/main/js/cortex.mjs), and let fibers await browser APIs with `cortex::web::Await`:

```cpp
// app.cpp — em++ with -sMODULARIZE=1 -sEXPORT_ES6=1, linked with cortex::cortex and cortex::web
#include <cortex/tiny_fiber/tiny_fiber.hpp>
#include <cortex/web/await.hpp>

#include <emscripten.h>

#include <memory>
#include <string>

namespace tf = cortex::tiny_fiber;
using emscripten::val;

namespace {
std::unique_ptr<tf::Scheduler> g_scheduler;
}

extern "C" EMSCRIPTEN_KEEPALIVE void* start() {
    g_scheduler = tf::Scheduler::Create([] {
        const std::string url = "/big.txt";
        // Only this fiber waits for the network; the page stays interactive.
        val response = cortex::web::Await(val::global("fetch")(url));
        std::string text = cortex::web::Await(response.call<val>("text")).as<std::string>();

        // Heavy work in a loop with tf::CheckPoint() is sliced automatically.
        auto words = tf::Spawn([&text] {
            long count = 0;
            for (char c : text) {
                count += c == ' ' ? 1 : 0;
                tf::CheckPoint();
            }
            return count;
        });
        EM_ASM({ console.log("words:", $0); }, words.Get());
    });
    return g_scheduler.get();
}
```

```js
import createModule from "./app.mjs";
import { drive } from "./cortex.mjs";

const Module = await createModule();
await drive(Module, Module._start(), { budgetMs: 8 }).done; // resolves when every fiber finished
```

`drive()` runs the fibers in 8 ms slices between browser tasks, sleeps while they sleep, wakes when a promise they await settles, and rejects `done` with a readable report if they deadlock or throw. The **[porting guide](docs/porting-guide.md)** walks through un-freezing an existing Emscripten app, and the **[guide](docs/guide.md)** covers every feature.

## What you get

| API | Purpose |
|---|---|
| `tiny_fiber::Scheduler` | Runs fibers on one thread. `Run()` blocks until done (returns the entry's result); `Create()` + `RunFor(budget)` for event loops. Reports `kRunnable / kWaiting / kDone / kDeadlocked`; a deadlock names each stuck fiber. |
| `Spawn` / `SpawnDetached` / `Future<T>` | Start fibers; `Get()`, `Wait()`, `WaitFor(timeout)`, `Cancel()`, `Detach()`. Exceptions travel through `Get()`; unobserved ones are never silently dropped. |
| `CheckPoint()` / `Yield()` / `SleepFor()` | Cooperative slicing (adaptive, a few ns when no yield is due, a no-op outside fibers), explicit yields, timers. |
| `Promise<T>` | A result delivered from outside fibers — a JavaScript callback, or another thread via `Scheduler::Post()`. |
| `WaitAll` / `WaitAny` / `Channel<T>` | Wait on several futures (or a container of them); FIFO message passing (bounded or unbounded, feedable from plain code). |
| `Mutex` / `ConditionVariable` | Cooperative sync primitives, with timed waits. |
| Cancellation | `Future::Cancel()` stops a fiber at its next wait or checkpoint, and the children it holds futures of. `Scheduler::Stop()` stops them all. |
| `cortex::web::Await(promise)` | A fiber awaits any JavaScript promise (WASM, embind). |
| `js/cortex.mjs` | Event-loop driver: time-boxed slices, timer-aware sleeping, wake-ups from JavaScript. |
| `Coroutine` / `Generator<T>` | The stackful primitives underneath: suspend/resume from any call depth; generators are input ranges. |

Natively (POSIX), fiber stacks from the scheduler's default memory resource sit above a guard page, so running off the end of a stack crashes on the spot instead of silently corrupting memory (a single frame larger than a page can still jump over it). The same sources build a native static library (Boost.Context) and a WebAssembly one (Emscripten, Asyncify).

**Learn more:** the **[guide](docs/guide.md)** explains the runtime feature by feature, the **[porting guide](docs/porting-guide.md)** un-freezes an existing Emscripten app, and [`examples/linux/runtime_tour.cpp`](https://github.com/GhevondW/cortex/blob/main/examples/linux/runtime_tour.cpp) runs every feature natively in half a second.

## Use it in your project

Requires C++20 and CMake 3.28.3+.

**FetchContent / CPM** — fetches Boost.Context on native builds:

```cmake
include(FetchContent)
FetchContent_Declare(cortex GIT_REPOSITORY https://github.com/GhevondW/cortex.git GIT_TAG main)
FetchContent_MakeAvailable(cortex)

target_link_libraries(app PRIVATE cortex::cortex)
# On Emscripten, also cortex::web for cortex::web::Await.
```

**Installed package** — build against a system Boost and install:

```bash
cmake -B build -DCORTEX_USE_SYSTEM_BOOST=ON -DCORTEX_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX=/opt/cortex
cmake --build build && cmake --install build
```

```cmake
find_package(cortex CONFIG REQUIRED)
target_link_libraries(app PRIVATE cortex::cortex)
```

For the browser, link with Emscripten as usual (`cortex::cortex` adds `-sASYNCIFY` and `-fexceptions`), build an ES module that keeps its runtime alive (`-sMODULARIZE=1 -sEXPORT_ES6=1`, and `-sEXIT_RUNTIME=0`, the default except with `-fsanitize=address`), and copy the driver next to your module: CMake's `${cortex_JS_DRIVER}` names it, for both `find_package` (installed under `share/cortex/`) and FetchContent. With the Emscripten toolchain, point `find_package` at the package with `-Dcortex_DIR=<prefix>/lib/cmake/cortex`.

## Good to know

- **Exports that run fibers:** under Asyncify, an exported function that switches fibers still finishes before JavaScript regains control, but the value it returns is a placeholder. Return results through a separate export or through memory — as `drive()` does.
- **Don't suspend inside a `catch` block.** C++ exception state is per thread, and fibers share the thread.
- **JavaScript exceptions must not unwind C++ frames.** Only promise rejections become C++ exceptions (`cortex::web::JsError`). A JavaScript exception thrown by a synchronous `val` call inside a fiber leaves the module unable to run fibers, and `drive()` then rejects with that error.
- **WASM stack depth:** suspending saves every frame's locals into a per-coroutine buffer (`CORTEX_WASM_ASYNCIFY_STACK_SIZE`, 64 KB by default, about 16–24 bytes per frame, 2–3× that with `-fsanitize=address`).
- **Behavior changes in this release** (for existing users):
  - `Scheduler::Run()` returns the entry's result and rethrows its exception; exceptions no one can observe (the `Create()` entry, detached fibers) are rethrown by `Step()`/`Run()` or passed to `Config::on_unhandled_exception`; `Run()` throws `DeadlockError` when fibers can never finish.
  - `IsDone()` means every fiber finished; use `GetStatus()` to tell waiting from deadlocked.
  - `Future::Get()`/`Wait()` called outside a fiber on an unfinished result throw a clear `std::logic_error`; inside a fiber they throw `SchedulerStoppingError` once the scheduler stops. A `Future` whose producer (a fiber or a `Promise`) is destroyed before delivering a result reports `BrokenPromiseError`. A fiber that has not started when its scheduler stops never runs.
  - `SchedulerStoppingError` derives from the new `CancelledError`. `ConditionVariable::Wait` leaves *without* the mutex re-locked when it throws one of these (unlike `std::condition_variable`).
  - `Spawn()` is `[[nodiscard]]` (dropping its `Future` waits for the fiber at once — use `SpawnDetached`); builds with `-Werror` must handle the result.
  - The scheduler's default memory resource puts a guard page under each stack on POSIX.
  - The unimplemented `cortex/async/*` headers moved to `experimental/` and are no longer installed.
  - WASM: each coroutine reserves a 64 KB Asyncify buffer (was 16 KB); set `CORTEX_WASM_ASYNCIFY_STACK_SIZE` to change it.

## Develop

Docker is the only requirement:

```bash
./dev.sh test-all       # native + WASM tests
./dev.sh video-editor   # build & serve the Video Editor → http://localhost:8080
./dev.sh serve          # serve the example bundle      → http://localhost:8080
./dev.sh help           # all commands
```

Building without Docker, the CMake options, packaging checks and IDE setup live in **[DEVELOPMENT.md](DEVELOPMENT.md)**.

## License

[MIT](https://github.com/GhevondW/cortex/blob/main/LICENSE)
