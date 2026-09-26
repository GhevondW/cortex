# Un-freeze your Emscripten app

Your C++ compiles to WebAssembly and runs — until it does something long. Then the tab stops painting, clicks queue up, and the browser offers to kill the page. This guide shows how to fix that with Cortex without restructuring your code into callbacks and without moving it to a Web Worker.

## Why the page freezes

JavaScript, rendering and input handling share the browser's main thread. A WebAssembly call is a JavaScript call: until it returns, nothing else happens. Any of these freezes the page:

- a long computation (image processing, a solver, parsing a big file);
- a blocking main loop (`while (running) { update(); render(); }`);
- a synchronous wait on something the browser only offers asynchronously (`fetch`, file pickers, IndexedDB, `requestAnimationFrame`).

Cortex runs such code in **fibers**: each has its own stack, so it can pause in the middle of any call chain and resume later. A scheduler runs fibers in short slices, and the browser gets the thread back between slices.

## Setup

1. Link `cortex::cortex` (and `cortex::web` if you await JavaScript promises). Linking adds `-sASYNCIFY` and `-fexceptions`.
2. Build your module as an ES module (`-sMODULARIZE=1 -sEXPORT_ES6=1`) and copy [`js/cortex.mjs`](../js/cortex.mjs) next to it.
3. Export a function that creates a scheduler and returns it:

```cpp
#include <cortex/tiny_fiber/tiny_fiber.hpp>

#include <emscripten.h>

#include <memory>

namespace tf = cortex::tiny_fiber;

namespace {
std::unique_ptr<tf::Scheduler> g_scheduler;
}

extern "C" EMSCRIPTEN_KEEPALIVE void* start_app() {
    g_scheduler = tf::Scheduler::Create([] {
        // Your existing entry point, unchanged: RunMyApp();
    });
    return g_scheduler.get();
}
```

4. Drive it from JavaScript:

```js
import createModule from "./app.mjs";
import { drive } from "./cortex.mjs";

const Module = await createModule();
const { done } = drive(Module, Module._start_app(), { budgetMs: 8 });
done.catch((error) => console.error(error)); // deadlocks and escaped exceptions end up here
```

`drive()` runs fibers for up to `budgetMs` per browser task, sleeps while fibers sleep, and wakes up when something they wait for arrives from JavaScript.

## Recipe 1 — a long computation

Add `tf::CheckPoint()` to the hot loops. It yields only when the fiber has used up its time slice (2 ms by default), costs about 2 ns otherwise, and does nothing when the code is not running in a fiber. The same function therefore keeps working in native builds, tests and tools.

```cpp
void Blur(const Image& in, Image& out) {
    for (int y = 0; y < in.height; ++y) {
        BlurRow(in, out, y);
        tf::CheckPoint(); // one check per row is plenty
    }
}
```

That is exactly how the [video editor demo](../apps/video_editor) filters frames while the video plays: the real filters call `CheckPoint()` once per row, and the cooperative renderer just runs them in a fiber.

For deep recursion, like a backtracking search, the checkpoint can live at any depth: fibers are stackful.

## Recipe 2 — a blocking main loop

Keep the loop and make its wait cooperative. `tf::SleepUntil` parks only the fiber, so the page keeps running:

```cpp
void MainLoop() {
    auto next_frame = tf::Scheduler::Current().Now();
    while (running) {
        Update();
        Render();
        next_frame += std::chrono::milliseconds(16);
        tf::SleepUntil(next_frame);
    }
}
```

To follow the display's own rhythm instead, await `requestAnimationFrame` (Recipe 3):

```js
Module.nextFrame = () => new Promise((resolve) => requestAnimationFrame(resolve));
```

```cpp
cortex::web::Await(emscripten::val::module_property("nextFrame")());
```

## Recipe 3 — awaiting a browser API

`cortex::web::Await` suspends the calling fiber until a JavaScript promise settles. Other fibers keep running, and the page stays interactive:

```cpp
#include <cortex/web/await.hpp>

using emscripten::val;

std::string LoadText(const std::string& url) {
    val response = cortex::web::Await(val::global("fetch")(url));
    if (!response["ok"].as<bool>()) {
        throw std::runtime_error("HTTP " + std::to_string(response["status"].as<int>()));
    }
    return cortex::web::Await(response.call<val>("text")).as<std::string>();
}
```

A rejected promise throws `cortex::web::JsError`. Your code reads top to bottom like the synchronous version it replaces.

## Recipe 4 — cancelling stale work

When the user moves a slider, the render for the old value is wasted work. Keep its `Future`, and cancel it:

```cpp
std::optional<tf::Future<Image>> render;

void OnSliderChanged(float value) { // runs in a fiber
    if (render) {
        render->Cancel(); // it stops at its next CheckPoint()
    }
    render = tf::Spawn([value] { return RenderPreview(value); });
}
```

A cancelled fiber throws `tf::CancelledError` at its next suspension point (`CheckPoint`, `Yield`, `SleepFor`, a wait), and it also cancels the fibers it spawned.

## Recipe 5 — events from the page into fibers

Bind a `tf::Channel` to the scheduler and feed it from exported functions called by your event handlers. `TrySend` never blocks, so it is safe outside fibers:

```cpp
std::unique_ptr<tf::Channel<Event>> g_events; // created with Channel<Event>(*g_scheduler)

extern "C" EMSCRIPTEN_KEEPALIVE void on_click(int x, int y) {
    g_events->TrySend(Event {x, y}); // wakes the fiber below, and the driver
}

void EventLoop() {
    for (Event& event : *g_events) { // receives until the channel is closed
        Handle(event);
    }
}
```

## Choosing a technique

| | Page stays responsive | Many concurrent tasks | Await JavaScript | Cancellation | C++ work touches DOM/canvas directly | What it takes |
|---|---|---|---|---|---|---|
| Asyncify + `emscripten_sleep` | while sleeping | no — one suspended call stack at a time | yes (`EM_ASYNC_JS`) | by hand | yes | `-sASYNCIFY` |
| `emscripten_set_main_loop` | if every iteration is short | by hand | no | by hand | yes | rewrite loops as callbacks |
| Web Workers / pthreads | yes | yes, truly parallel | inside each worker | by hand | no — needs messages or proxying | `-pthread`, COOP/COEP headers for `SharedArrayBuffer` |
| **Cortex** | yes, time-sliced | yes, fibers | yes, only the waiting fiber pauses | `Future::Cancel`, `Scheduler::Stop` | yes | link cortex, use `js/cortex.mjs` |

Cortex and workers complement each other: workers for parallel number crunching that never touches the page, Cortex for everything that has to live on the main thread, or that you simply don't want to restructure.

## Pitfalls

- **Return values of exports that run fibers are unreliable.** Under Asyncify such an export returns to JavaScript before its real call completes. Poll state through a separate export (as `drive()` does with `cortex_scheduler_last_status`), or write results to memory.
- **Don't suspend inside a `catch` block.** Exception state is per thread, and all fibers share one thread.
- **Deep recursion that suspends** needs Asyncify buffer space (`CORTEX_WASM_ASYNCIFY_STACK_SIZE`, 64 KB by default, about 16–24 bytes per frame) as well as C stack (`Scheduler::Config::default_stack_size`, 256 KB by default).
- **Don't call `Scheduler::Run()` in the browser.** It blocks until every fiber finishes. Use `Create()` + `drive()`.
- **Debugging a hang:** name fibers with `tf::SetFiberName("loader")`. A deadlock report, or `Scheduler::DescribeFibers()`, then lists each fiber and what it is waiting in.
