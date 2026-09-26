# tiny_fiber guide

`cortex::tiny_fiber` runs C++ in **fibers**: lightweight threads of execution with their own stacks, scheduled cooperatively on a single OS thread. A fiber runs until it reaches a suspension point (a wait, a sleep, a `CheckPoint()` whose time slice is spent), then another fiber runs. Nothing is preempted, so fibers need no locks around plain data, and ordinary synchronous-looking code can pause in the middle of any call chain.

The same code runs natively (Boost.Context) and in WebAssembly (Emscripten, Asyncify). In the browser the scheduler is driven from the JavaScript event loop, so heavy C++ shares the main thread with rendering and input instead of freezing the page.

```cpp
#include <cortex/tiny_fiber/tiny_fiber.hpp> // everything below

namespace tf = cortex::tiny_fiber;
using namespace std::chrono_literals;
```

**Contents:**
1. [Running a scheduler](#running-a-scheduler)
2. [Spawning fibers and futures](#spawning-fibers-and-futures)
3. [Sharing the thread: CheckPoint() and time slices](#sharing-the-thread-checkpoint-and-time-slices)
4. [Time: sleeping and timeouts](#time-sleeping-and-timeouts)
5. [Cancellation and stopping](#cancellation-and-stopping)
6. [Waiting for several futures](#waiting-for-several-futures)
7. [Channels](#channels)
8. [Promises, event loops and other threads](#promises-event-loops-and-other-threads)
9. [Mutex and ConditionVariable](#mutex-and-conditionvariable)
10. [When things go wrong](#when-things-go-wrong)
11. [Configuration](#configuration)
12. [Running in the browser](#running-in-the-browser)
13. [Coroutine and Generator](#coroutine-and-generator)
14. [Examples to read](#examples-to-read)

## Running a scheduler

A `Scheduler` owns the fibers and runs them on the thread that drives it. There are two ways to drive one.

**`Scheduler::Run(entry)` blocks until every fiber has finished** and returns what `entry` returned. Use it in native programs, tests and tools:

```cpp
const long primes = tf::Scheduler::Run([] {
    auto work = tf::Spawn([] { return CountPrimes(400'000); });
    return work.Get(); // suspends this fiber, not the thread
});
```

If `entry` throws, `Run()` rethrows it. If fibers are left that can never finish, `Run()` throws `DeadlockError`, which names them (see [When things go wrong](#when-things-go-wrong)).

**`Scheduler::Create(entry)` + `RunFor(budget)` lets an event loop drive the scheduler.** `RunFor()` never blocks: it runs fibers until the budget is spent or nothing can run, and returns what the scheduler can do next:

| `Status` | Meaning | What the loop does |
|---|---|---|
| `kRunnable` | Fibers are ready to run now. | Call `RunFor()` again soon, e.g. after rendering a frame. |
| `kWaiting` | Fibers sleep, or wait for the outside world (a `Promise` fulfilled or a `Channel` fed from plain code). | Sleep until `NextTimerDeadline()`, or until the wake-up handler fires. |
| `kDone` | Every fiber finished. | Stop. |
| `kDeadlocked` | Fibers are suspended and nothing can ever wake them. | Report `DescribeFibers()`; it is a bug. |

```cpp
auto scheduler = tf::Scheduler::Create([] { RunMyApp(); });
for (bool running = true; running;) {
    switch (scheduler->RunFor(4ms)) {
        case tf::Scheduler::Status::kRunnable:
            break; // render a frame, handle input...
        case tf::Scheduler::Status::kWaiting:
            if (auto deadline = scheduler->NextTimerDeadline()) {
                std::this_thread::sleep_until(*deadline);
            }
            break;
        case tf::Scheduler::Status::kDone:
            running = false;
            break;
        case tf::Scheduler::Status::kDeadlocked:
            std::fprintf(stderr, "%s\n", scheduler->DescribeFibers().c_str());
            running = false;
            break;
    }
}
```

In the browser, [`js/cortex.mjs`](https://github.com/GhevondW/cortex/blob/main/js/cortex.mjs)'s `drive()` is that loop (see [Running in the browser](#running-in-the-browser)). `Step()` runs a single fiber until it suspends, for finer control, and `GetStatus()`, `IsDone()` and `GetFiberCount()` report the state at any time. Call `Step()` and `RunFor()` from plain code, never from one of the scheduler's own fibers.

A scheduler and its fibers belong to one thread. The only member other threads may call is `Post()` (see [Promises, event loops and other threads](#promises-event-loops-and-other-threads)). Schedulers nest: a fiber may run a scheduler of its own, and `Scheduler::Current()` then names the innermost one.

## Spawning fibers and futures

`tf::Spawn(fn)` starts a fiber and returns a `Future` for its result. It may be called from any fiber, and uses the current scheduler:

```cpp
tf::Future<std::string> page = tf::Spawn([] { return Download("/index.html"); });
DoSomethingElse();                  // runs while the download waits
std::string html = page.Get();      // waits for it, or rethrows its exception
```

- `Get()` waits and returns the value (once; it is moved out), or rethrows the fiber's exception.
- `Wait()` only waits. For `Future<void>`, which has no value, `Wait()` also rethrows.
- `IsReady()` checks without waiting; `WaitFor(timeout)` and `WaitUntil(deadline)` wait with a limit and return whether the result is ready.
- `Get()` and `Wait()` suspend the calling *fiber*. From plain code they work only once the result is ready; before that they throw `std::logic_error` rather than block the thread.

**Futures join.** Destroying a `Future` whose fiber is still running, from a fiber of the same scheduler, waits for it. A child fiber therefore never outlives the scope that spawned it, and may safely use that scope's locals by reference. This is also why `Spawn` is `[[nodiscard]]`: dropping the `Future` waits for the fiber right away.

For fire-and-forget work, use `tf::SpawnDetached(fn)`, or call `Detach()` on a `Future`. Nobody waits for a detached fiber, so an exception escaping it is reported as unhandled: `Step()`/`RunFor()`/`Run()` rethrow it, or `Config::on_unhandled_exception` receives it. Cancellations are not reported.

Both functions take an optional stack size: `tf::Spawn(fn, 64 * 1024)`.

## Sharing the thread: CheckPoint() and time slices

A fiber keeps the thread until it suspends. For waits that is automatic. **Long computations should call `tf::CheckPoint()`** in their hot loops:

```cpp
void Blur(const Image& in, Image& out) {
    for (int y = 0; y < in.height; ++y) {
        BlurRow(in, out, y);
        tf::CheckPoint(); // one check per row is plenty
    }
}
```

- `CheckPoint()` yields only when the fiber has used up its **time slice** (`Config::time_slice`, 2 ms by default, cut short by the deadline of a running `RunFor()`). Otherwise it costs a few nanoseconds: it reads the clock only every so many calls, at most 32, adapting to how long iterations take at each call site.
- Outside a fiber it does nothing. The same function therefore works when called directly, in tests, in tools, and in a fiber. That is the key to porting existing code: add checkpoints, then run the code in a fiber.
- Keep single iterations well below the time slice. If the iterations at one call site suddenly get much more expensive, the yield can come up to 32 of them late.
- For deep recursion, like a backtracking search, the checkpoint can live at any depth: fibers are stackful.

`tf::Yield()` always gives the other ready fibers a turn, and `tf::YieldIfOthersReady()` does so only when something else could run (another ready fiber, a due timer, posted work). All three are cancellation points.

## Time: sleeping and timeouts

```cpp
tf::SleepFor(250ms);                                    // this fiber only
tf::SleepUntil(tf::Scheduler::Current().Now() + 1s);    // an absolute deadline
if (!download.WaitFor(5s)) { /* timed out */ }
```

Sleeping fibers cost nothing: `Run()` blocks the thread until the next timer is due, and `RunFor()` reports `kWaiting` with `NextTimerDeadline()`. A huge duration such as `hours::max()` means "forever" (until cancelled). Timed waits exist on `Future` (`WaitFor`, `WaitUntil`) and `ConditionVariable` (`WaitFor`, `WaitUntil`).

Time comes from `Config::clock`, a `steady_clock` by default. Tests can install a function returning a fake time, and advance it by hand, to test timeouts deterministically.

## Cancellation and stopping

`future.Cancel()` asks a fiber to stop. It throws `tf::CancelledError` at its next **cancellation point**, and is woken if it is waiting in one:

- `CheckPoint()`, `Yield()`, `YieldIfOthersReady()`;
- `SleepFor()`, `SleepUntil()`;
- `Future::Wait()`, `Get()`, `WaitFor()`, `WaitUntil()`, `WaitAll()`, `WaitAny()`;
- `ConditionVariable` waits, and `Channel::Send()` / `Receive()` / iteration;
- `cortex::web::Await()`.

`Mutex::Lock()` is not one. A fiber that was cancelled before it started never runs. `Get()` on the cancelled fiber's future rethrows the `CancelledError`, unless the fiber caught it.

```cpp
std::optional<tf::Future<Image>> render;

void OnViewChanged(View view) { // runs in a fiber
    if (render) {
        render->Cancel(); // stops at its next CheckPoint()
        render.reset();   // and waits for that, releasing what it used
    }
    render = tf::Spawn([view] { return Render(view); });
}
```

**Cancellation reaches children.** When a cancelled fiber unwinds, the `Future`s it holds are destroyed. Each one cancels and joins its fiber, so the whole tree of fibers it spawned stops. Detached fibers are not cancelled.

Code without cancellation points can poll `tf::IsCancellationRequested()` and bail out early. To clean up, catch `CancelledError`, then rethrow it or return.

**Stopping a scheduler.** `Scheduler::Stop()`, which destruction also calls, stops every fiber. From then on each suspension point throws `SchedulerStoppingError`, a `CancelledError`, so the fibers unwind. Joins still wait, so children never outlive their parents' scopes. `tf::IsStopping()` tells a fiber that shutdown has begun.

## Waiting for several futures

```cpp
auto config = tf::Spawn(LoadConfig);
auto assets = tf::Spawn(LoadAssets);
tf::WaitAll(config, assets); // or a container: tf::WaitAll(futures)

std::vector<tf::Future<std::string>> mirrors = AskAllMirrors();
const std::size_t first = tf::WaitAny(mirrors); // index of a ready one
std::string answer = mirrors[first].Get();
for (auto& mirror : mirrors) {
    mirror.Cancel(); // the others are no longer needed
}
```

`WaitAll` and `WaitAny` accept futures as separate arguments or in any container (`std::vector`, `std::array`, `std::span`). They do not rethrow the fibers' exceptions; call `Get()` for those. `WaitAny` returns the lowest index among the ready futures, and does not cancel the others.

## Channels

`tf::Channel<T>` is a FIFO queue between fibers. `Send()` suspends while a bounded channel is full, and `Receive()` suspends while it is empty. `Close()` ends the stream: receivers drain what is buffered and then get `std::nullopt`, and range-`for` stops.

```cpp
tf::Channel<Job> jobs(/*capacity=*/8); // bounded; default: unbounded
auto producer = tf::Spawn([&jobs] {
    for (Job& job : LoadJobs()) {
        jobs.Send(std::move(job)); // waits while 8 are queued
    }
    jobs.Close();
});
auto worker = tf::Spawn([&jobs] {
    for (Job& job : jobs) { // receives until closed and drained
        Run(job);
    }
});
```

`TrySend()`, `TryReceive()` and `Close()` never suspend, so **plain code can feed fibers**: an event callback between scheduler steps, or a JavaScript handler calling an export. A channel meant for that is created with its scheduler, `tf::Channel<Event> events(scheduler)`. While fibers wait on such a channel the scheduler reports `kWaiting`, not `kDeadlocked`, and its wake-up handler fires when a value arrives. A channel is not thread-safe; other threads go through `Post()`. Like every primitive here, a channel must outlive the fibers that use it.

## Promises, event loops and other threads

A `tf::Promise<T>` is a result that is delivered from outside any fiber: a JavaScript callback, a completion handler, another thread. Fibers wait on its future:

```cpp
tf::Promise<Response> promise(scheduler); // from plain code; tf::Promise<T>() inside a fiber
tf::Future<Response> response = promise.GetFuture();

// Later, on the scheduler's thread (e.g. in an event callback):
promise.SetValue(std::move(result)); // or SetException(...)
```

- Destroying an unfulfilled promise completes its future with `BrokenPromiseError`. This also happens when a fiber that captured the promise finishes, or is cancelled, without fulfilling it.
- While fibers wait on a promise, the scheduler reports `kWaiting`. A blocking native `Run()` keeps waiting for it, like `std::future::wait`, because another thread may fulfil it. In single-threaded WebAssembly nothing can arrive while `Run()` blocks, so there `Run()` throws `DeadlockError`. Use `Create()` + a driver instead.
- A promise belongs to its scheduler's thread: create, fulfil and destroy it there.

**`Scheduler::Post(work)`** is the one thread-safe entry point. It queues `work` to run on the scheduler's thread, outside any fiber, at the start of its next step. A blocking `Run()` wakes up for it. Other threads use it to hand results to fibers:

```cpp
tf::Scheduler::Run([] {
    tf::Promise<long> promise;
    tf::Future<long> result = promise.GetFuture();
    std::thread worker([&scheduler = tf::Scheduler::Current(), promise = std::move(promise)]() mutable {
        const long value = ExpensiveComputation(); // truly parallel
        scheduler.Post([promise = std::move(promise), value]() mutable { promise.SetValue(value); });
    });
    std::printf("%ld\n", result.Get()); // only this fiber waits
    worker.join();
});
```

`Scheduler::SetWakeupHandler(fn)` is for event-loop integrations. It is called when work becomes runnable while nobody is stepping the scheduler (a promise fulfilled, a channel fed, a `Post()`), so the loop can schedule the next `RunFor()` instead of polling. `drive()` uses it.

## Mutex and ConditionVariable

Fibers on one thread never run at the same time, so plain data needs no lock between suspension points. `tf::Mutex` is for critical sections that *contain* suspension points:

```cpp
tf::Mutex mutex;
tf::ConditionVariable ready;
bool loaded = false;

auto guard = tf::Lock(mutex);
ready.Wait(guard, [&] { return loaded; });              // wait until loaded
bool ok = ready.WaitFor(guard, 2s, [&] { return loaded; }); // with a timeout
```

`NotifyOne()` and `NotifyAll()` must be called from a fiber. Unlike `std::condition_variable`, a wait that ends because the fiber was cancelled or the scheduler stops throws **without** re-locking: the mutex is released and the guard no longer owns it. For fiber-to-fiber hand-offs, a `Channel` or a `Promise` is usually simpler.

## When things go wrong

**Exceptions are never silently dropped.**
- An exception escaping a fiber is stored in its `Future`, and `Get()` rethrows it.
- The `Run()` entry's exception is rethrown by `Run()`.
- Exceptions nobody can observe (the `Create()` entry, detached fibers) are rethrown by `Step()`/`RunFor()`/`Run()`, or passed to `Config::on_unhandled_exception` when it is set.

**Deadlocks are reported.** When every remaining fiber is suspended and nothing can wake them, `Run()` throws `DeadlockError` and `RunFor()` returns `kDeadlocked`. The message lists each stuck fiber and what it waits in. Name fibers with `tf::SetFiberName()` to make it readable:

```text
tiny_fiber: deadlock - every remaining fiber is suspended and nothing can wake it.
2 live fiber(s):
  #1 "client" suspended in Channel::Receive
  #2 "server" suspended in Channel::Receive
```

`Scheduler::DescribeFibers()` prints the same list at any time, which is useful for a debug overlay or a hang.

**Other errors:**
- `BrokenPromiseError`: the producer (a promise, or a fiber destroyed before finishing) is gone.
- `std::logic_error`: an API was used outside a fiber where it needs one, or `Get()` was called twice.

**Stack overflows fault immediately** natively: the default memory resource puts a guard page under each fiber stack (POSIX), so running off the end crashes at once instead of corrupting memory. A single frame larger than a page can still jump over it. Fiber stacks in WebAssembly have no guard.

**Pitfalls:**
- **Don't suspend inside a `catch` block.** C++ exception state is per thread, and fibers share the thread: store what you need, leave the handler, then wait.
- **Keep single iterations between checkpoints well below the time slice**, or the thread is held that long.
- **Primitives must outlive the fibers that use them.** Declaring them before the futures in the same scope does it, since futures join on destruction.

## Configuration

`tf::Scheduler::Config` is passed to `Run()` or `Create()`:

| Field | Default | Purpose |
|---|---|---|
| `default_stack_size` | 256 KiB | Stack of each fiber (`Spawn` can override per fiber). |
| `memory_resource` | `MakeDefaultFiberResource()` | Where stacks and fiber state come from: a per-scheduler pool, over guard-paged stacks on POSIX. |
| `on_unhandled_exception` | empty (rethrow) | Receives exceptions nobody observes, instead of `Step()`/`Run()` rethrowing them. |
| `clock` | `steady_clock::now` | Source of time for timers, budgets and slices (inject a fake one in tests). |
| `time_slice` | 2 ms | How long a fiber runs before `CheckPoint()` yields. |

```cpp
tf::Scheduler::Config config;
config.time_slice = 1ms;
config.on_unhandled_exception = [](std::exception_ptr error) { LogError(error); };
auto scheduler = tf::Scheduler::Create(RunMyApp, config);
```

`cortex::MakeGuardedStackResource()` and `cortex::HasGuardPageSupport()` expose the guard-paged allocator for your own resources. In WebAssembly, each coroutine also reserves an Asyncify buffer (`CORTEX_WASM_ASYNCIFY_STACK_SIZE`, 64 KB by default, a CMake cache variable). It bounds how deep a fiber may be when it suspends: a few thousand frames, fewer under `-fsanitize=address`.

## Running in the browser

**Build.**
- Link `cortex::cortex`, which adds `-sASYNCIFY` and `-fexceptions`, and `cortex::web` if fibers await JavaScript promises (adds embind).
- Build an ES module (`-sMODULARIZE=1 -sEXPORT_ES6=1`), and keep the runtime alive after start-up (`-sEXIT_RUNTIME=0`). That is the default, except with `-fsanitize=address`.
- Copy the driver next to your module. Its path is `${cortex_JS_DRIVER}` in CMake, both for `find_package(cortex)` and for `add_subdirectory` / FetchContent.

**Export a function that creates the scheduler**, and drive it:

```cpp
std::unique_ptr<tf::Scheduler> g_scheduler;

extern "C" EMSCRIPTEN_KEEPALIVE void* start_app() {
    g_scheduler = tf::Scheduler::Create([] { RunMyApp(); });
    return g_scheduler.get();
}
```

```js
import createModule from "./app.js";
import { drive } from "./cortex.mjs";

const Module = await createModule();
const { done, stop } = drive(Module, Module._start_app(), { budgetMs: 8 });
done.catch((error) => console.error(error)); // deadlocks, escaped exceptions
```

**`drive(Module, scheduler, { budgetMs })`** runs the fibers from the event loop:
- **Slices:** while fibers are runnable, it runs them for up to `budgetMs` (default 8) per macrotask, so input and rendering interleave.
- **Sleeping and waking:** while they sleep it waits for the next timer, and while they wait on the outside world it sleeps until the scheduler's wake-up handler fires.
- **One drive per scheduler:** driving a scheduler that is already being driven throws.

It returns `{ done, stop }`:
- **`done` resolves** when every fiber finished, when `stop()` is called, or when the scheduler is destroyed from C++.
- **`done` rejects** with an `Error`:
  - on a deadlock: the message lists the stuck fibers;
  - when an exception escapes a fiber nobody waits for: its `what()`;
  - when a trap or a JavaScript exception interrupts a fiber. That leaves the module unable to run fibers, so every other drive on it rejects too, and later `drive()` calls reject at once.
- **`stop()`** stops driving and resolves `done`. It does not stop the fibers: to cancel the work itself, call an export that calls `Scheduler::Stop()` or `Future::Cancel()`, or destroy the scheduler.

`Status` mirrors `Scheduler::Status`.

**Await JavaScript promises** with `cortex::web::Await` (header `<cortex/web/await.hpp>`, target `cortex::web`). Only the calling fiber waits; the page and the other fibers keep running:

```cpp
using emscripten::val;
val response = cortex::web::Await(val::global("fetch")(std::string("/data.json")));
if (!response["ok"].as<bool>()) {
    throw std::runtime_error("HTTP " + std::to_string(response["status"].as<int>()));
}
val data = cortex::web::Await(response.call<val>("json"));
```

A rejected promise throws `cortex::web::JsError`; `what()` carries the reason and `Reason()` returns the JavaScript value.

**Feed fibers from page events** through a channel created with the scheduler, from exports your event handlers call:

```cpp
std::unique_ptr<tf::Channel<Click>> g_clicks; // = std::make_unique<tf::Channel<Click>>(*g_scheduler)

extern "C" EMSCRIPTEN_KEEPALIVE void on_click(int x, int y) {
    g_clicks->TrySend(Click {x, y}); // wakes the waiting fiber and the driver
}
```

**Browser pitfalls:**
- **JavaScript exceptions must not unwind C++ frames.** Only promise rejections become C++ exceptions. A JavaScript exception thrown by a synchronous `val` call (`JSON.parse` on bad input, a DOM exception, a failed `as<T>()`) unwinds WebAssembly without running destructors. In a fiber it leaves the module unable to run fibers. Check values before converting them, or run code that may throw inside a promise and `Await()` it.
- **The return value of an export that switches fibers is a placeholder.** Under Asyncify, such an export still finishes before JavaScript regains control, but the value it returns is not the real one. Return results through memory or a separate export, as `drive()` does with `cortex_scheduler_last_status()`.
- **Don't call the blocking `Scheduler::Run()` in the browser.** Use `Create()` + `drive()`.

The [porting guide](porting-guide.md) walks through un-freezing an existing Emscripten app step by step.

## Coroutine and Generator

The fibers are built on two lower-level primitives, usable on their own.

`cortex::Coroutine` is a stackful coroutine: a function that can suspend from any call depth and be resumed by its caller:

```cpp
auto coroutine = cortex::Coroutine::Make([](cortex::CoroutineSuspendContext& context) {
    Step1();
    context.Suspend(); // back to whoever called Resume()
    Step2();
});
coroutine.Resume(); // runs Step1
coroutine.Resume(); // runs Step2
```

`cortex::Generator<T>` yields a sequence of values and is an input range:

```cpp
auto squares = cortex::Generator<int>::Make([](auto& yield) {
    for (int i = 1; i <= 5; ++i) {
        yield(i * i);
    }
});
for (int value : squares) {
    std::printf("%d\n", value);
}
```

## Examples to read

- [`examples/linux/runtime_tour.cpp`](https://github.com/GhevondW/cortex/blob/main/examples/linux/runtime_tour.cpp): a native tour, one feature per section. It covers time slicing, timeouts and cancellation, channels, `WaitAny`, `Promise` + `Post()` from a thread, a deadlock report, and a `RunFor()` loop. Build with `-DCORTEX_BUILD_EXAMPLES=ON` and run `runtime_tour`.
- [`examples/wasm/responsive_demo.cpp`](https://github.com/GhevondW/cortex/blob/main/examples/wasm/responsive_demo.cpp): the **Responsive C++** browser demo. A Mandelbrot explorer rendered by fibers with `CheckPoint()`, cancellation of stale renders, a channel fed by clicks, `Await(fetch())`, `SleepFor()` and a deadlock report, next to a "plain call" mode that freezes the page. [Live](https://GhevondW.github.io/cortex/examples/responsive_demo.html).
- [`examples/wasm/fiber_workflow.cpp`](https://github.com/GhevondW/cortex/blob/main/examples/wasm/fiber_workflow.cpp): a producer and worker fibers over a bounded channel, visualised.
- [`apps/video_editor`](https://github.com/GhevondW/cortex/tree/main/apps/video_editor): a real app. Video filters call `CheckPoint()` once per row, and a cooperative renderer runs them in a fiber while the video plays.
