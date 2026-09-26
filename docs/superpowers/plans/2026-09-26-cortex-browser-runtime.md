# Cortex: Fixes + Browser Runtime Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn Cortex from "a stackful-coroutine library with nice demos" into a C++ runtime that makes code behave in the browser: consumable by any CMake project, free of silent failures, and able to time-slice, sleep, cancel, and await JavaScript without freezing the page — with the same code running natively.

**Architecture:** All new behavior lives in `cortex::tiny_fiber` (single-threaded, one scheduler per thread). The scheduler gains a clock, a timer map, a posted-work queue, and a status model (`Runnable / Waiting / Done / Deadlocked`). Every suspension goes through one primitive — *park with a waiter token (fiber id + wait epoch)* — so futures, promises, mutexes, condition variables, channels, timers and cancellation share one wake-up path that can never wake the wrong wait. A thin WASM layer (C exports + `js/cortex.mjs`) drives the scheduler from the JS event loop in budgeted slices.

**Tech Stack:** C++20 (library contract), C++23 for the repo's own tests/apps, CMake ≥ 3.28 + CPM, Boost.Context (native), Emscripten + Asyncify (WASM), GoogleTest, Node.js (WASM tests), Docker (`./dev.sh`) for the WASM toolchain.

**Spec:** The review in the originating conversation (2026-09-26). Its verified findings are restated in "Findings addressed" below so this plan stands alone.

## Findings addressed

| # | Finding (verified) | Task |
|---|---|---|
| F1 | `add_subdirectory`/FetchContent consumers fail: `src/CMakeLists.txt:53` uses `CMAKE_SOURCE_DIR`; tests/apps leak into consumer builds; no install/export | 1, 2 |
| F2 | `cortex::async` is ~20 public headers of stubs that throw "Not implemented yet" | 3 |
| F3 | Headers are C++17/20-clean but the project advertises C++23 only | 1, 20 |
| F4 | `Scheduler::Run` silently swallows the entry fiber's exception | 7 |
| F5 | Deadlock is reported as success (`Run()` returns, `IsDone()` true) | 7 |
| F6 | Stepping a scheduler inside another scheduler's fiber breaks the outer one | 4 |
| F7 | `Future::Get()` outside a fiber doesn't wait, then throws "Fiber completed without result" | 6 |
| F8 | Waiter validation (`IsSuspended()`) is not generation-safe → possible wrong-wait wake-ups | 5 |
| F9 | Native fiber stacks have no guard page → stack overflow silently corrupts memory | 8 |
| F10 | Asyncify buffer hard-coded to 16 KB | 10 |
| F11 | `Generator` isn't a range | 9 |
| F12 | README's JS loop `while (Module._step()) {}` runs the whole job in one frame | 17, 20 |
| F13 | No timers, no time budget, no cancellation, no external await → apps hand-roll JS loops and duplicate code | 11–19 |

## Global Constraints

- Public headers must compile as C++20 (`target_compile_features(cortex PUBLIC cxx_std_20)`); the repo's own build keeps `CMAKE_CXX_STANDARD 23`.
- No new third-party dependencies. Only `function2` and Boost.Context (native) remain; Boost is fetched with `BOOST_INCLUDE_LIBRARIES context` only.
- Everything in `tiny_fiber` stays single-threaded except `Scheduler::Post()`, which is the one thread-safe entry point.
- Spawn/yield hot paths must not regress more than the CI tolerance (`BENCH_MAX_REGRESSION` 1.30).
- Existing public API keeps compiling. Deliberate behavior changes are listed in "Behavior changes" and in the README.
- Every code change: native build + full `ctest` green; WASM-affecting changes also pass `./dev.sh test-wasm`.
- **Git:** the user's global rule — never run a state-changing git command without asking first. "Commit" steps below mean: ask the user, then commit.

## Behavior changes (intentional)

1. `Scheduler::Run(f)` returns `f`'s result and rethrows its exception; throws `DeadlockError` if fibers can never finish.
2. `Step()` rethrows exceptions escaping detached/entry fibers unless `Config::on_unhandled_exception` is set.
3. `IsDone()` means "every fiber finished" (was: "nothing in the ready queue"). Use `GetStatus()` to detect `kDeadlocked`/`kWaiting`.
4. `Future::Wait/Get` from outside a fiber on an unfinished result throws `std::logic_error` with a clear message.
5. `Future::Wait/Get` are stop-aware: they throw `SchedulerStoppingError` once the scheduler is stopping.
6. A fiber destroyed before producing its result leaves `BrokenPromiseError` in its Future.
7. A spawned fiber that has not started when the scheduler stops never runs its body.
8. `SchedulerStoppingError` now derives from `CancelledError` (still a `std::runtime_error`).
9. The scheduler's default memory resource puts a guard page under each stack on POSIX.

## Review Focus

- A Future / Promise outliving its scheduler, then Wait/Get/IsReady/destroy is called → no use-after-free; ready futures return their value; others throw a clear error. (Task 6 tests.)
- A JS promise settling after its scheduler was destroyed → no crash, callback memory freed. (Task 18 test `settle after scheduler destroyed`.)
- `Stop()` while fibers wait in timers, channels, promises and WaitAny at once → every fiber exits, `IsDone()` becomes true, no stale timer fires into a destroyed fiber. (Task 15 test `StopWakesEveryKindOfWait`.)
- Cancelling a fiber that is parked in two queues (e.g. `WaitAny` over a channel receive and a timer) → exactly one wake, no double-enqueue. (Tasks 5/13/14 tests.)
- `CheckPoint()` called from plain (non-fiber) code and from a nested scheduler's fiber → no-op outside fibers; yields the innermost fiber inside. (Task 12 tests.)

---

## File structure

```
include/cortex/
  guarded_stack_resource.hpp            NEW  guard-paged stack allocations (POSIX)
  generator.hpp                         MOD  input-range support
  tiny_fiber/
    tiny_fiber.hpp                      MOD  umbrella includes new headers
    scheduler.hpp                       MOD  status, clock, timers, Post, describe, nesting
    yield.hpp                           MOD  + CheckPoint, SleepFor/Until, IsCancellationRequested, SetFiberName
    future.hpp                          MOD  state-based Future, Spawn, SpawnDetached
    promise.hpp                         NEW  Promise<T> (completable from outside fibers)
    wait.hpp                            NEW  WaitAll / WaitAny
    channel.hpp                         NEW  Channel<T>
    mutex.hpp, condition_variable.hpp   MOD  waiter tokens, CV WaitFor/WaitUntil
    errors/cancelled_error.hpp          NEW
    errors/deadlock_error.hpp           NEW
    errors/broken_promise_error.hpp     NEW
    errors/scheduler_stopping_error.hpp MOD  derives from CancelledError
    detail/fiber.hpp                    MOD  epoch, cancel flag, name, wait reason, timer handle
    detail/wait_queue.hpp               NEW  WaiterRef, WaitQueue (FIFO), WaiterList (small)
    detail/future_state.hpp             NEW  shared state for Future/Promise
  web/await.hpp                         NEW  cortex::web::Await(emscripten::val) (WASM only)
src/
  guarded_stack_resource.cpp            NEW
  tiny_fiber/scheduler.cpp              MOD  (+ WASM driver exports under __EMSCRIPTEN__)
  tiny_fiber/fiber.cpp, yield.cpp, mutex.cpp, condition_variable.cpp  MOD
  tiny_fiber/wait_queue.cpp             NEW
  web/await.cpp                         NEW
  detail/coroutine_emscripten_impl.cpp  MOD  configurable Asyncify buffer
js/cortex.mjs                           NEW  browser/node driver
experimental/async/...                  MOVED from include/cortex/async, src/async, tests/async_test.cpp
tests/
  CMakeLists.txt                        MOD  cortex_add_test() helper
  tiny_fiber_nested_test.cpp            NEW  (Task 4)
  tiny_fiber_future_test.cpp            NEW  (Tasks 6, 14, 16)
  tiny_fiber_errors_test.cpp            NEW  (Task 7)
  guarded_stack_test.cpp                NEW  (Task 8)
  tiny_fiber_timer_test.cpp             NEW  (Tasks 11, 12)
  tiny_fiber_cancel_test.cpp            NEW  (Task 13)
  tiny_fiber_channel_test.cpp           NEW  (Task 15)
  wasm_asyncify_test.cpp                NEW  (Task 10, WASM only)
  web/driver_test.cpp, web/driver_test.mjs  NEW  (Tasks 17, 18, WASM only)
  package/                              NEW  consumer projects (Tasks 1, 2)
apps/video_editor/...                   MOD  (Task 19) CheckPoint in filters, delete duplicated math
README.md, DEVELOPMENT.md, docs/porting-guide.md   MOD/NEW (Task 20)
.github/workflows/ci.yml, Dockerfile, docker-compose.yml   MOD (Task 21)
```

Verification commands used throughout (native, from repo root):

```bash
SP=<scratch dir>; D=$PWD/cmake-build-debug/_deps   # reuse already-downloaded sources
cmake -S . -B $SP/build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCORTEX_BUILD_TESTS=ON \
  -DCPM_Boost_SOURCE=$D/boost-src -DCPM_function2_SOURCE=$D/function2-src -DCPM_GTest_SOURCE=$D/gtest-src
cmake --build $SP/build && ctest --test-dir $SP/build --output-on-failure
```

WASM: `./dev.sh test-wasm` (Docker). Sanitizers: add `-DCORTEX_USE_SANITIZERS=ON` to a separate build dir.

---

# Phase 1 — Packaging & hygiene

### Task 1: Consumable as a CMake subproject, C++20 contract

**Files:**
- Modify: `CMakeLists.txt`, `src/CMakeLists.txt:52-55`, `cmake/Dependencies.cmake:11`, `cmake/CompilerConfig.cmake:1-4`
- Create: `tests/package/subdir/CMakeLists.txt`, `tests/package/main.cpp`, `tests/package/check_subdirectory.sh`

**Interfaces:**
- Produces: options `CORTEX_BUILD_TESTS/EXAMPLES/APPS/BENCHMARKS` default to `${PROJECT_IS_TOP_LEVEL}` (tests) / OFF; target `cortex::cortex` carries `cxx_std_20`.

- [ ] **Step 1: Write the consumer smoke project** — `tests/package/main.cpp`:

```cpp
// Minimal consumer used by the packaging checks: must build with only the
// cortex::cortex target and C++20.
#include <cortex/tiny_fiber/tiny_fiber.hpp>

#include <cstdio>

namespace tf = cortex::tiny_fiber;

int main() {
    const int answer = tf::Scheduler::Run([] {
        auto a = tf::Spawn([] {
            tf::Yield();
            return 6;
        });
        return a.Get() * 7;
    });
    std::printf("%d\n", answer);
    return answer == 42 ? 0 : 1;
}
```

`tests/package/subdir/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.28)
project(cortex_subdir_consumer CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_subdirectory(${CORTEX_SOURCE_DIR} cortex)
add_executable(consumer ../main.cpp)
target_link_libraries(consumer PRIVATE cortex::cortex)
```

`tests/package/check_subdirectory.sh` configures it (forwarding `CPM_*_SOURCE` / `CPM_SOURCE_CACHE`), builds `consumer`, runs it, and asserts no `video_editor`/`gtest` target exists in the consumer build (`cmake --build ... --target help | grep -c video_editor` == 0).

Note: `Scheduler::Run` returning a value lands in Task 7. Until then use `int answer = 0; tf::Scheduler::Run([&]{ ... answer = ...; });` and switch to the return form in Task 7.

- [ ] **Step 2: Run it — expect FAIL** with `'cortex/base_coroutine.hpp' file not found`.
- [ ] **Step 3: Fix CMake**
  - `src/CMakeLists.txt`: `$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>`; add `target_compile_features(cortex PUBLIC cxx_std_20)`.
  - `CMakeLists.txt`: `option(CORTEX_BUILD_TESTS "Build unit tests" ${PROJECT_IS_TOP_LEVEL})`; add `apps/` only when `CORTEX_BUILD_APPS OR (CORTEX_BUILD_TESTS AND PROJECT_IS_TOP_LEVEL)`.
  - `cmake/CompilerConfig.cmake`: only set `CMAKE_CXX_STANDARD 23` when `PROJECT_IS_TOP_LEVEL` and the variable is not already defined.
  - `cmake/Dependencies.cmake`: `BOOST_INCLUDE_LIBRARIES context` (drop `asio`); GTest only when `CORTEX_BUILD_TESTS`.
  - Also build the library once with `-DCMAKE_CXX_STANDARD=20` to prove the sources are C++20-clean.
- [ ] **Step 4: Run check + full native suite — expect PASS** (consumer prints `42`).
- [ ] **Step 5: Commit** (ask first): `build: make cortex consumable via add_subdirectory/FetchContent`.

### Task 2: System Boost + install/export (`find_package(cortex)`)

**Files:**
- Modify: `CMakeLists.txt`, `cmake/Dependencies.cmake`, `src/CMakeLists.txt`
- Create: `cmake/cortexConfig.cmake.in`, `tests/package/installed/CMakeLists.txt`, `tests/package/check_install.sh`

**Interfaces:**
- Produces: options `CORTEX_USE_SYSTEM_BOOST` (OFF), `CORTEX_INSTALL` (ON when top-level); exported target `cortex::cortex`; package `cortex` with `find_dependency(function2)` and (native) `find_dependency(Boost CONFIG COMPONENTS context)`.

- [ ] **Step 1: Consumer** `tests/package/installed/CMakeLists.txt`: `find_package(cortex CONFIG REQUIRED)` + same `main.cpp`.
- [ ] **Step 2: `check_install.sh`**: configure cortex with `-DCORTEX_USE_SYSTEM_BOOST=ON -DCORTEX_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX=$TMP/prefix`, `cmake --install`, then configure the consumer with `-DCMAKE_PREFIX_PATH=$TMP/prefix`, build, run → expect FAIL (no install rules).
- [ ] **Step 3: Implement**
  - `Dependencies.cmake`: `if(CORTEX_USE_SYSTEM_BOOST) find_package(Boost 1.74 CONFIG REQUIRED COMPONENTS context) else() CPMAddPackage(...)`.
  - `CORTEX_INSTALL` requires system Boost on native (Boost fetched with `BOOST_SKIP_INSTALL_RULES` cannot be exported) → `message(FATAL_ERROR ...)` with the fix spelled out if both fetch + install are requested explicitly; when defaulted, silently disable install.
  - `install(TARGETS cortex EXPORT cortexTargets ...)`, `install(DIRECTORY include/cortex ...)`, `install(EXPORT cortexTargets NAMESPACE cortex:: ...)`, `configure_package_config_file` + `write_basic_package_version_file(... COMPATIBILITY SameMajorVersion)`. function2's own install rules are enabled for the fetched copy.
- [ ] **Step 4: Run `check_install.sh` — expect PASS**; full native suite PASS.
- [ ] **Step 5: Commit** (ask first): `build: install/export rules and CORTEX_USE_SYSTEM_BOOST`.

### Task 3: Move the `cortex::async` stubs out of the shipped library

**Files:**
- Move: `include/cortex/async/**` → `experimental/async/include/cortex/async/**`; `src/async/**` → `experimental/async/src/**`; `tests/async_test.cpp` → `experimental/async/tests/async_test.cpp`
- Create: `experimental/CMakeLists.txt`, `experimental/README.md`
- Modify: `src/CMakeLists.txt` (drop async sources), `tests/CMakeLists.txt` (drop async test), `CMakeLists.txt` (`option(CORTEX_BUILD_EXPERIMENTAL ... OFF)`)

`cortex::exec` stays: it is real, tested code.

- [ ] **Step 1:** `grep -rn "cortex/async" --include=*.{cpp,hpp,txt,cmake,dox,md}` — record every reference.
- [ ] **Step 2:** Move the files with plain `mv` (not `git mv`), add `experimental/CMakeLists.txt` building `cortex_async_experimental` (STATIC, links `cortex::cortex`) + its test when `CORTEX_BUILD_EXPERIMENTAL`.
- [ ] **Step 3:** Build default config → PASS, and `grep -rn "cortex/async" include src tests` → 0 hits. Build with `-DCORTEX_BUILD_EXPERIMENTAL=ON` → experimental test PASS.
- [ ] **Step 4: Commit** (ask first): `chore: move unimplemented cortex::async API to experimental/`.

---

# Phase 2 — Correctness

### Task 4: Nested schedulers restore the outer "current scheduler"

**Files:** Modify `src/tiny_fiber/scheduler.cpp`, `include/cortex/tiny_fiber/scheduler.hpp`; Create `tests/tiny_fiber_nested_test.cpp`; Modify `tests/CMakeLists.txt` (add `cortex_add_test(name source)` helper that handles native `gtest_discover_tests` and WASM `.js` + node `add_test` + link options, and convert existing tests to it).

**Interfaces:** Produces `static Scheduler* Scheduler::TryCurrent() noexcept;` and private RAII `CurrentSchedulerScope` used by `Step()`, the destructor, and posted-work draining.

- [ ] **Step 1: Failing tests**

```cpp
#include <cortex/tiny_fiber/tiny_fiber.hpp>
#include <gtest/gtest.h>

namespace tf = cortex::tiny_fiber;

TEST(TinyFiberNested, OuterFiberCanYieldAfterSteppingInnerScheduler) {
    bool outer_yield_ok = false;
    tf::Scheduler::Run([&] {
        auto inner = tf::Scheduler::Create([] { tf::Yield(); });
        while (inner->Step()) {
        }
        tf::Yield(); // must yield the OUTER fiber
        outer_yield_ok = true;
    });
    EXPECT_TRUE(outer_yield_ok);
}

TEST(TinyFiberNested, CurrentIsInnermostInsideInnerFiber) {
    tf::Scheduler::Run([] {
        tf::Scheduler* outer = &tf::Scheduler::Current();
        tf::Scheduler* seen_inside = nullptr;
        auto inner = tf::Scheduler::Create([&] { seen_inside = &tf::Scheduler::Current(); });
        while (inner->Step()) {
        }
        EXPECT_EQ(seen_inside, inner.get());
        EXPECT_EQ(&tf::Scheduler::Current(), outer);
    });
}

TEST(TinyFiberNested, RunInsideFiber) {
    int inner_result = 0;
    tf::Scheduler::Run([&] {
        tf::Scheduler::Run([&] {
            auto f = tf::Spawn([] { tf::Yield(); return 5; });
            inner_result = f.Get();
        });
        tf::Yield();
    });
    EXPECT_EQ(inner_result, 5);
}

TEST(TinyFiberNested, TryCurrentIsNullOutsideFibers) {
    EXPECT_EQ(tf::Scheduler::TryCurrent(), nullptr);
}
```

- [ ] **Step 2: Run → FAIL** ("No scheduler is running").
- [ ] **Step 3: Implement** a `CurrentSchedulerScope { Scheduler* previous; explicit CurrentSchedulerScope(Scheduler* s) : previous(std::exchange(g_current_scheduler, s)) {} ~CurrentSchedulerScope() { g_current_scheduler = previous; } }` in `scheduler.cpp`; use it in `Step()` and `~Scheduler()`; delete `assert(g_current_scheduler == nullptr)` in `RunLoop`; add `TryCurrent()`.
- [ ] **Step 4: Run → PASS**, full suite PASS.
- [ ] **Step 5: Commit** (ask first).

### Task 5: Waiter tokens (fiber id + wait epoch) for every wait queue

**Files:** Create `include/cortex/tiny_fiber/detail/wait_queue.hpp`, `src/tiny_fiber/wait_queue.cpp`; Modify `detail/fiber.hpp`, `fiber.cpp`, `scheduler.hpp/.cpp`, `mutex.hpp/.cpp`, `condition_variable.hpp/.cpp`.

**Interfaces (produces):**

```cpp
namespace cortex::tiny_fiber::detail {
using FiberId = std::uint64_t;

// One specific suspension of one fiber. A fiber's epoch advances every time it
// prepares to wait, so a token recorded for an earlier wait never wakes a later one.
struct WaiterRef {
    FiberId id {0};
    std::uint64_t epoch {0};
};

// FIFO of waiters (Mutex, ConditionVariable, Channel).
class WaitQueue {
public:
    void Push(WaiterRef ref);
    bool WakeOne(Scheduler& scheduler);   // wakes the first still-valid waiter; false if none
    std::size_t WakeAll(Scheduler& scheduler);
    [[nodiscard]] bool Empty() const noexcept;
private:
    std::deque<WaiterRef> waiters_;
};

// Small unordered waiter set for futures (usually 0-1 waiters, stored inline).
class WaiterList {
public:
    void Push(WaiterRef ref);
    void WakeAll(Scheduler& scheduler);   // also clears
    void Clear() noexcept;
private:
    std::array<WaiterRef, 2> inline_ {};
    std::uint8_t inline_count_ {0};
    std::vector<WaiterRef> overflow_;
};
} // namespace

// Scheduler (internal, friend-accessible):
detail::WaiterRef PrepareWait();                   // current fiber: ++epoch, returns token
void ParkCurrent(const char* reason, bool cancellable);  // Running -> Suspended, suspend
bool WakeIfWaiting(detail::WaiterRef ref);         // id alive && Suspended && epoch matches
void WakeFiber(detail::Fiber* fiber);              // unconditional wake of a suspended fiber
```

`Fiber` gains `std::uint64_t wait_epoch_`, `const char* wait_reason_`, `bool cancellable_park_`; loses `AddWaiter/ForEachWaiter/inline_waiters_` (joining moves to the future state in Task 6).

- [ ] **Step 1:** Existing Mutex/CV/Stop tests are the safety net (`TinyFiberMutexTest.*`, `TinyFiberCondVarTest.*`, `TinyFiberStoppingTest.*`) — run them green first. The wrong-wait scenario becomes directly testable once timers exist; Task 14 adds `CvWaitForTimeoutLeavesNoStaleWake` and Task 13 adds `CancelDuringWaitAnyWakesOnce`.
- [ ] **Step 2: Implement.** Mutex::Lock: `waiters_.Push(scheduler.PrepareWait()); scheduler.ParkCurrent("Mutex::Lock", false);`. Unlock: `waiters_.WakeOne(scheduler)`. CV::Wait: `Push(PrepareWait())` before unlocking, `ParkCurrent("ConditionVariable::Wait", true)`. NotifyOne/All → `WakeOne/WakeAll`. `Stop()` wakes via `WakeFiber` for every suspended fiber.
- [ ] **Step 3: Run full suite → PASS.** Also the sanitizer build.
- [ ] **Step 4: Commit** (ask first).

### Task 6: State-based Future (+ BrokenPromise, clear errors, Detach, SpawnDetached)

**Files:** Create `detail/future_state.hpp`, `errors/broken_promise_error.hpp`, `tests/tiny_fiber_future_test.cpp`; Modify `future.hpp`, `scheduler.hpp/.cpp`, test files that discard `Spawn` results.

**Interfaces (produces):**

```cpp
namespace cortex::tiny_fiber {
class BrokenPromiseError : public std::runtime_error { using runtime_error::runtime_error; };

namespace detail {
struct FutureStateBase {
    Scheduler* scheduler {nullptr};
    std::weak_ptr<void> alive;        // scheduler liveness
    FiberId fiber_id {0};             // 0 for promise-backed state
    bool ready {false};
    bool retrieved {false};
    bool detached {false};
    bool external {false};            // completed from outside fibers (Promise)
    std::exception_ptr exception;
    WaiterList waiters;
    [[nodiscard]] Scheduler* LiveScheduler() const noexcept;
    void MarkReady();                 // ready=true, wake waiters
    void Fail(std::exception_ptr ex); // detached → Scheduler::ReportUnhandled unless CancelledError
    void Abandon();                   // Fail(BrokenPromiseError) if not ready
};
template <typename T> struct FutureState : FutureStateBase { std::optional<T> result; };
template <> struct FutureState<void> : FutureStateBase {};
template <typename T> std::shared_ptr<FutureState<T>> MakeFutureState(Scheduler& s, bool external);
template <typename R, typename F> auto MakeSpawnBody(std::shared_ptr<FutureState<R>> state, F&& f);
}

template <typename T> class Future {
public:
    T Get();                        // void specialization: void Get()
    void Wait();                    // Future<void>::Wait rethrows (unchanged); Future<T>::Wait does not
    [[nodiscard]] bool IsReady() const noexcept;
    void Detach() noexcept;         // destructor no longer joins; exceptions go to unhandled handler
    // WaitFor/WaitUntil (Task 14), Cancel (Task 13)
};

template <typename F> [[nodiscard]] auto Spawn(F&& f) -> Future<std::invoke_result_t<F>>;
template <typename F> [[nodiscard]] auto Spawn(F&& f, std::size_t stack_size) -> Future<std::invoke_result_t<F>>;
template <typename F> void SpawnDetached(F&& f);
template <typename F> void SpawnDetached(F&& f, std::size_t stack_size);
}
```

Spawn body (the only place fiber results are produced):

```cpp
template <typename R, typename F>
auto MakeSpawnBody(std::shared_ptr<FutureState<R>> state, F&& func) {
    return [state = std::move(state), f = std::forward<F>(func)]() mutable {
        try {
            ThrowIfInterruptedAtStart(); // stopping or cancelled before the body ever ran
            if constexpr (std::is_void_v<R>) {
                f();
                state->MarkReady();
            } else {
                state->result.emplace(f());
                state->MarkReady();
            }
        } catch (const cortex::detail::ForcedUnwind&) {
            state->Abandon(); // BrokenPromiseError, never the internal sentinel
            throw;
        } catch (...) {
            state->Fail(std::current_exception());
        }
    };
}
```

Wait loop (shared by Wait/Get/WaitFor/destructor join):

```cpp
// cancellable=false is used by the destructor's join.
void WaitImpl(bool cancellable) {
    if (state_->ready) return;
    Scheduler* s = state_->LiveScheduler();
    if (s == nullptr) throw std::logic_error("Future: result not ready and its scheduler was destroyed");
    if (Scheduler::TryCurrent() != s || s->GetCurrentFiber() == nullptr) {
        throw std::logic_error(
            "Future: result is not ready and the caller is not a fiber of the owning scheduler. "
            "Call Get()/Wait() from a fiber, or drive the scheduler until IsReady().");
    }
    while (!state_->ready) {
        s->ThrowIfInterrupted(cancellable);
        state_->waiters.Push(s->PrepareWait());
        detail::ExternalWaitScope external(*s, state_->external);
        s->ParkCurrent("Future::Wait", cancellable);
    }
}
```

`~Scheduler`: move `fiber_slots_` into a local before clearing so wakes triggered by `Abandon()` during teardown resolve to "no such fiber".

- [ ] **Step 1: Failing tests** (`tests/tiny_fiber_future_test.cpp`):

```cpp
TEST(TinyFiberFuture, GetOutsideFiberOnUnfinishedResultThrowsClearError) {
    std::optional<tf::Future<int>> escaped;
    auto s = tf::Scheduler::Create([&] { escaped.emplace(tf::Spawn([] { tf::Yield(); return 1; })); });
    s->Step();
    try {
        (void)escaped->Get();
        FAIL() << "expected logic_error";
    } catch (const std::logic_error& e) {
        EXPECT_NE(std::string(e.what()).find("not a fiber"), std::string::npos);
    }
    while (s->Step()) {}
    EXPECT_EQ(escaped->Get(), 1); // finished → Get works from anywhere
}

TEST(TinyFiberFuture, FiberDestroyedBeforeFinishingYieldsBrokenPromise) {
    std::optional<tf::Future<int>> escaped;
    {
        auto s = tf::Scheduler::Create([&] {
            escaped.emplace(tf::Spawn([] {
                tf::Mutex m; tf::ConditionVariable cv; auto g = tf::Lock(m);
                cv.Wait(g); // parked forever → force-unwound or stop-woken at teardown
                return 1;
            }));
        });
        s->Step(); s->Step();
    }
    ASSERT_TRUE(escaped->IsReady());
    EXPECT_THROW((void)escaped->Get(), tf::CancelledError); // SchedulerStoppingError is-a CancelledError
}

TEST(TinyFiberFuture, WaitIsStopAware) {
    bool threw = false;
    auto s = tf::Scheduler::Create([&] {
        auto child = tf::Spawn([] { for (;;) tf::Yield(); });
        try { child.Wait(); } catch (const tf::SchedulerStoppingError&) { threw = true; }
    });
    s->Step(); s->Step();
    s->Stop();
    while (!s->IsDone()) s->Step();
    EXPECT_TRUE(threw);
}

TEST(TinyFiberFuture, DetachedFutureDoesNotJoin) {
    std::vector<int> order;
    tf::Scheduler::Run([&] {
        {
            auto f = tf::Spawn([&] { tf::Yield(); order.push_back(2); });
            f.Detach();
        } // no join
        order.push_back(1);
    });
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
}

TEST(TinyFiberFuture, SpawnDetachedRuns) {
    int ran = 0;
    tf::Scheduler::Run([&] { tf::SpawnDetached([&] { tf::Yield(); ++ran; }); });
    EXPECT_EQ(ran, 1);
}

TEST(TinyFiberFuture, UnstartedFiberNeverRunsBodyAfterStop) {
    bool body_ran = false;
    {
        auto s = tf::Scheduler::Create([&] {
            auto f = tf::Spawn([&] { body_ran = true; });
            f.Detach();
        });
        s->Step(); // entry spawns child, child not started
    }              // teardown
    EXPECT_FALSE(body_ran);
}
```

(`CancelledError` exists from Task 7's error headers; land `errors/cancelled_error.hpp` + the `SchedulerStoppingError` base change in this task.)

- [ ] **Step 2: Run → FAIL.**
- [ ] **Step 3: Implement** per the interfaces; update tests that discard `tf::Spawn(...)` to `(void)tf::Spawn(...)` or `SpawnDetached`.
- [ ] **Step 4: Run full native + sanitizer suites → PASS**; run benchmarks and compare with the pre-change CSV (`spawn_join` must stay within 1.30×).
- [ ] **Step 5: Commit** (ask first).

### Task 7: No silent failures — Run result, unhandled exceptions, status, deadlock report

**Files:** Create `errors/deadlock_error.hpp`, `tests/tiny_fiber_errors_test.cpp`; Modify `scheduler.hpp/.cpp`, `yield.hpp/.cpp`, `detail/fiber.hpp`.

**Interfaces (produces):**

```cpp
class DeadlockError : public std::runtime_error { using runtime_error::runtime_error; };

class Scheduler {
public:
    enum class Status : std::uint8_t { kRunnable, kWaiting, kDone, kDeadlocked };
    struct Config {
        std::size_t default_stack_size = Coroutine::kDefaultStackSizeBytes;
        MemoryResourceSharedPtr memory_resource = MakeDefaultFiberResource();
        std::function<void(std::exception_ptr)> on_unhandled_exception; // empty → rethrown by Step()/Run()
        // time_slice and clock are added in Tasks 11/12
    };
    template <typename F> static auto Run(F&& entry) -> std::invoke_result_t<F>;
    template <typename F> static auto Run(F&& entry, Config config) -> std::invoke_result_t<F>;
    [[nodiscard]] Status GetStatus() const;
    [[nodiscard]] bool IsDone() const noexcept;              // live fiber count == 0
    [[nodiscard]] std::size_t GetFiberCount() const noexcept; // live fibers
    [[nodiscard]] std::string DescribeFibers() const;
    /// @cond INTERNAL
    void ReportUnhandledInternal(std::exception_ptr ex);
    /// @endcond
};
MemoryResourceSharedPtr MakeDefaultFiberResource(); // pooled (guarded from Task 8)
void SetFiberName(std::string_view name);          // yield.hpp, current fiber
```

`Run` shape (teardown always happens with no exception in flight):

```cpp
template <typename F>
auto Scheduler::Run(F&& entry, Config config) -> std::invoke_result_t<F> {
    using R = std::invoke_result_t<F>;
    std::shared_ptr<detail::FutureState<R>> state;
    std::exception_ptr failure;
    {
        Scheduler scheduler(std::move(config));
        state = detail::MakeFutureState<R>(scheduler, false);
        scheduler.SpawnFiberInternal(detail::MakeSpawnBody<R>(state, std::forward<F>(entry)),
                                     scheduler.config_.default_stack_size);
        try {
            if (scheduler.RunToCompletion() == Status::kDeadlocked) {
                failure = std::make_exception_ptr(DeadlockError(scheduler.DeadlockMessage()));
            }
        } catch (...) {
            failure = std::current_exception(); // unhandled exception from a detached fiber
        }
    }
    if (failure) std::rethrow_exception(failure);
    return detail::TakeResult(*state); // rethrows the entry's own exception
}
```

`DescribeFibers()` format (one line per live fiber, human sequence number = `id >> kSlotIndexBits`):

```
3 live fiber(s):
  #1 "main"     suspended in Future::Wait
  #4 "decoder"  suspended in ConditionVariable::Wait
  #5            ready
```

- [ ] **Step 1: Failing tests** (`tests/tiny_fiber_errors_test.cpp`):

```cpp
TEST(TinyFiberErrors, RunRethrowsEntryException) {
    EXPECT_THROW(tf::Scheduler::Run([] { throw std::runtime_error("disk full"); }), std::runtime_error);
}

TEST(TinyFiberErrors, RunReturnsEntryValue) {
    EXPECT_EQ(tf::Scheduler::Run([] { return 42; }), 42);
}

TEST(TinyFiberErrors, RunReportsDeadlockWithDescription) {
    try {
        tf::Scheduler::Run([] {
            tf::SetFiberName("stuck-main");
            tf::Mutex m; tf::ConditionVariable cv; auto g = tf::Lock(m);
            cv.Wait(g);
        });
        FAIL() << "expected DeadlockError";
    } catch (const tf::DeadlockError& e) {
        const std::string msg = e.what();
        EXPECT_NE(msg.find("deadlock"), std::string::npos);
        EXPECT_NE(msg.find("stuck-main"), std::string::npos);
        EXPECT_NE(msg.find("ConditionVariable::Wait"), std::string::npos);
    }
}

TEST(TinyFiberErrors, StepRethrowsDetachedFiberException) {
    auto s = tf::Scheduler::Create([] { tf::SpawnDetached([] { throw std::runtime_error("boom"); }); });
    EXPECT_THROW({ while (s->Step()) {} }, std::runtime_error);
}

TEST(TinyFiberErrors, UnhandledHandlerReceivesException) {
    std::string seen;
    tf::Scheduler::Config cfg;
    cfg.on_unhandled_exception = [&](std::exception_ptr ex) {
        try { std::rethrow_exception(ex); } catch (const std::exception& e) { seen = e.what(); }
    };
    auto s = tf::Scheduler::Create([] { throw std::runtime_error("create-entry"); }, cfg);
    EXPECT_NO_THROW({ while (s->Step()) {} });
    EXPECT_EQ(seen, "create-entry");
}

TEST(TinyFiberErrors, StoppingIsNotReportedAsUnhandled) {
    auto s = tf::Scheduler::Create([] { for (;;) tf::Yield(); });
    s->Step();
    s->Stop();
    EXPECT_NO_THROW({ while (!s->IsDone()) s->Step(); });
}

TEST(TinyFiberErrors, StatusDistinguishesDoneAndDeadlocked) {
    auto s = tf::Scheduler::Create([] { tf::Mutex m; tf::ConditionVariable cv; auto g = tf::Lock(m); cv.Wait(g); });
    while (s->Step()) {}
    EXPECT_EQ(s->GetStatus(), tf::Scheduler::Status::kDeadlocked);
    EXPECT_FALSE(s->IsDone());
    EXPECT_EQ(s->GetFiberCount(), 1u);
    auto d = tf::Scheduler::Create([] {});
    while (d->Step()) {}
    EXPECT_EQ(d->GetStatus(), tf::Scheduler::Status::kDone);
}
```

- [ ] **Step 2: Run → FAIL.** — **Step 3: Implement.** — **Step 4: full native + sanitizer PASS; update repro expectations; switch `tests/package/main.cpp` to the return form.** — **Step 5: Commit** (ask first).

### Task 8: Guard pages under fiber stacks

**Files:** Create `include/cortex/guarded_stack_resource.hpp`, `src/guarded_stack_resource.cpp`, `tests/guarded_stack_test.cpp`; Modify `src/CMakeLists.txt`, `src/tiny_fiber/scheduler.cpp` (`MakeDefaultFiberResource`).

**Interfaces (produces):**

```cpp
namespace cortex {
/// True when MakeGuardedStackResource() really installs guard pages (POSIX native).
[[nodiscard]] bool HasGuardPageSupport() noexcept;

struct GuardedStackResourceConfig {
    /// Requests at least this large are treated as stacks and get a guard page.
    std::size_t min_guarded_bytes = 16u * 1024u;
    MemoryResourceSharedPtr upstream = GetDefaultMemoryResource(); ///< small allocations
};

/// Stack-sized blocks come from mmap with one PROT_NONE page directly below the
/// returned pointer (stacks grow down), so an overflow faults instead of
/// corrupting neighbouring memory. Smaller blocks go to `upstream`.
MemoryResourceSharedPtr MakeGuardedStackResource(GuardedStackResourceConfig config = {});
}
```

`MakeDefaultFiberResource()` = `MakePooledMemoryResource({.upstream = MakeGuardedStackResource()})` when `HasGuardPageSupport()`, else the plain pool. Pooled blocks keep their guard page because the pool recycles whole blocks.

- [ ] **Step 1: Failing tests**

```cpp
TEST(GuardedStack, SupportedOnPosix) { EXPECT_TRUE(cortex::HasGuardPageSupport()); }

TEST(GuardedStack, LargeBlocksAreUsableAndSmallBlocksPassThrough) {
    auto r = cortex::MakeGuardedStackResource();
    auto* big = static_cast<unsigned char*>(r->Allocate(256 * 1024));
    big[0] = 1; big[256 * 1024 - 1] = 2;
    r->Deallocate(big, 256 * 1024);
    void* small = r->Allocate(64);
    r->Deallocate(small, 64);
}

TEST(GuardedStackDeathTest, WritingBelowAStackFaults) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_DEATH({
        auto r = cortex::MakeGuardedStackResource();
        auto* p = static_cast<volatile unsigned char*>(r->Allocate(64 * 1024));
        p[-1] = 1; // guard page
    }, "");
}

namespace {
int Recurse(int n) {
    volatile char frame[1024];
    frame[0] = static_cast<char>(n);
    return n == 0 ? frame[0] : Recurse(n - 1) + frame[0];
}
}

TEST(GuardedStackDeathTest, FiberStackOverflowCrashesInsteadOfCorrupting) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_DEATH({
        tf::Scheduler::Config cfg;
        cfg.default_stack_size = 64 * 1024;
        tf::Scheduler::Run([] { (void)Recurse(1'000'000); }, cfg);
    }, "");
}
```

- [ ] **Step 2: Run → FAIL** (header missing). — **Step 3: Implement** (`mmap`/`mprotect`/`munmap`, page size via `sysconf(_SC_PAGESIZE)`, round up, `std::bad_alloc` on failure). Compile the POSIX branch under `#if !defined(__EMSCRIPTEN__) && (defined(__unix__) || defined(__APPLE__))`. — **Step 4: native + sanitizer suites PASS; benchmark spawn numbers within tolerance.** — **Step 5: Commit** (ask first).

### Task 9: Generator is an input range

**Files:** Modify `include/cortex/generator.hpp`, `tests/generator_test.cpp`, `examples/linux/binary_tree.cpp`.

- [ ] **Step 1: Failing tests**

```cpp
static_assert(std::ranges::input_range<cortex::Generator<int>>);

TEST(GeneratorRangeTest, RangeForVisitsEveryValue) {
    auto gen = cortex::Generator<int>::Make([](auto& yield) { for (int i = 0; i < 4; ++i) yield(i); });
    std::vector<int> seen;
    for (int v : gen) seen.push_back(v);
    EXPECT_EQ(seen, (std::vector<int>{0, 1, 2, 3}));
}

TEST(GeneratorRangeTest, NestedYieldsFromRecursion) { /* in-order tree walk yields from nested calls */ }

TEST(GeneratorRangeTest, EmptyGeneratorHasNoElements) {
    auto gen = cortex::Generator<int>::Make([](auto&) {});
    EXPECT_EQ(std::ranges::distance(gen.begin(), gen.end()), 0);
}
```

- [ ] **Step 2: FAIL. Step 3: Implement** `class Iterator` (input iterator: `value_type`, `difference_type`, `T& operator*() const`, `++`, `operator==(std::default_sentinel_t)`), `begin()`, `end() → std::default_sentinel`. Update the binary-tree example to range-for. **Step 4: PASS. Step 5: Commit** (ask first).

### Task 10: Configurable Asyncify buffer (WASM)

**Files:** Modify `src/detail/coroutine_emscripten_impl.cpp:18`, `cmake/WasmConfig.cmake`, `CMakeLists.txt`; Create `tests/wasm_asyncify_test.cpp` (WASM-only).

- `set(CORTEX_WASM_ASYNCIFY_STACK_SIZE 65536 CACHE STRING "...")` → `target_compile_definitions(cortex PRIVATE CORTEX_WASM_ASYNCIFY_STACK_SIZE=...)`; `#ifndef CORTEX_WASM_ASYNCIFY_STACK_SIZE / #define 65536`.
- Default raised 16 KB → 64 KB after measuring (Step 1). Document the per-fiber memory cost.

- [ ] **Step 1: Measure** with a WASM test that recurses to depth N inside a coroutine and suspends at the bottom; bisect the maximum N that works at 16 KB (expected: a few hundred frames).
- [ ] **Step 2: Test** asserts depth 2000 works with the new default and documents how to raise it.
- [ ] **Step 3: `./dev.sh test-wasm` PASS. Step 4: Commit** (ask first).

---

# Phase 3 — Runtime features

### Task 11: Clock, timers, RunFor/RunUntil, Waiting status

**Files:** Modify `scheduler.hpp/.cpp`, `yield.hpp/.cpp`, `detail/fiber.hpp`; Create `tests/tiny_fiber_timer_test.cpp`.

**Interfaces (produces):**

```cpp
class Scheduler {
public:
    using Clock = std::chrono::steady_clock;
    using Duration = Clock::duration;
    using TimePoint = Clock::time_point;
    struct Config { /* … */ TimePoint (*clock)() = &Clock::now; };
    Status RunFor(Duration budget);          // run until budget spent or nothing runnable
    Status RunUntil(TimePoint deadline);
    [[nodiscard]] std::optional<TimePoint> NextTimerDeadline() const;
    [[nodiscard]] TimePoint Now() const;     // config clock
};
template <typename Rep, typename Period> void SleepFor(std::chrono::duration<Rep, Period> d);
template <typename C, typename D> void SleepUntil(std::chrono::time_point<C, D> tp); // converted to Scheduler::Clock
```

Timers: `std::multimap<TimePoint, FiberId> timers_`; a sleeping fiber stores its iterator (`timer_`, `timer_armed_`); `WakeFiber` erases an armed timer so no stale timers exist. `Step()` fires due timers first (reads the clock only when `!timers_.empty()`). `RunToCompletion()` (used by `Run`) blocks natively with `post_cv_.wait_until(next deadline)` when `GetStatus() == kWaiting`.

- [ ] **Step 1: Failing tests** (fake clock for determinism):

```cpp
namespace { tf::Scheduler::TimePoint g_now{}; tf::Scheduler::TimePoint FakeNow() { return g_now; } }

TEST(TinyFiberTimer, SleepingFiberWaitsForItsDeadline) {
    g_now = {};
    tf::Scheduler::Config cfg; cfg.clock = &FakeNow;
    bool woke = false;
    auto s = tf::Scheduler::Create([&] { tf::SleepFor(std::chrono::milliseconds(10)); woke = true; }, cfg);
    EXPECT_EQ(s->RunFor(std::chrono::milliseconds(1)), tf::Scheduler::Status::kWaiting);
    EXPECT_EQ(s->NextTimerDeadline(), g_now + std::chrono::milliseconds(10));
    g_now += std::chrono::milliseconds(9);
    EXPECT_EQ(s->RunFor(std::chrono::milliseconds(1)), tf::Scheduler::Status::kWaiting);
    EXPECT_FALSE(woke);
    g_now += std::chrono::milliseconds(1);
    EXPECT_EQ(s->RunFor(std::chrono::milliseconds(1)), tf::Scheduler::Status::kDone);
    EXPECT_TRUE(woke);
}

TEST(TinyFiberTimer, TimersFireInDeadlineOrder) { /* three fibers sleep 30/10/20 → order 10,20,30 */ }

TEST(TinyFiberTimer, RunBlocksUntilTimersFire) {   // real clock
    const auto start = std::chrono::steady_clock::now();
    tf::Scheduler::Run([] { tf::SleepFor(std::chrono::milliseconds(20)); });
    EXPECT_GE(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(20));
}

TEST(TinyFiberTimer, SleepForZeroYields) { /* ping-pong ordering like Yield */ }

TEST(TinyFiberTimer, StopWakesSleepersAndClearsTimers) {
    auto s = tf::Scheduler::Create([] { tf::SleepFor(std::chrono::hours(1)); });
    s->Step();
    ASSERT_TRUE(s->NextTimerDeadline().has_value());
    s->Stop();
    while (!s->IsDone()) s->Step();
    EXPECT_FALSE(s->NextTimerDeadline().has_value());
}

TEST(TinyFiberTimer, RunForReturnsWhenBudgetSpent) { /* fiber yields forever; RunFor(2ms) returns kRunnable after ≥2ms */ }
```

- [ ] **Step 2: FAIL. Step 3: Implement. Step 4: PASS (native + sanitizer + wasm). Step 5: Commit** (ask first).

### Task 12: `CheckPoint()` — budget-aware yield

**Files:** Modify `scheduler.hpp/.cpp`, `yield.hpp/.cpp`; tests in `tests/tiny_fiber_timer_test.cpp`.

**Interfaces (produces):** `void CheckPoint();` and `Config::time_slice` (`Duration`, default 2 ms).

```cpp
// Scheduler::CheckPointCurrent — called by tf::CheckPoint() when inside a fiber.
void Scheduler::CheckPointCurrent() {
    ThrowIfInterrupted(/*cancellable=*/true);
    const bool new_slice = checkpoint_slice_ != step_count_;
    if (!new_slice && --checkpoint_countdown_ > 0) return;
    const TimePoint now = config_.clock();
    if (new_slice) {
        checkpoint_slice_ = step_count_;
        slice_deadline_ = std::min(now + config_.time_slice, run_deadline_);
    } else {
        // Aim for ~16 clock reads per slice: cheap loops read the clock rarely,
        // expensive iterations still yield on time.
        const auto since = now - last_checkpoint_;
        const auto target = config_.time_slice / 16;
        if (since < target / 2 && checkpoint_stride_ < kMaxCheckpointStride) checkpoint_stride_ *= 2;
        else if (since > target * 2 && checkpoint_stride_ > 1) checkpoint_stride_ /= 2;
    }
    last_checkpoint_ = now;
    checkpoint_countdown_ = checkpoint_stride_;
    if (now >= slice_deadline_) YieldCurrent();
}
```

- [ ] **Step 1: Failing tests**

```cpp
TEST(TinyFiberCheckPoint, NoOpOutsideFibers) { EXPECT_NO_THROW(tf::CheckPoint()); }

TEST(TinyFiberCheckPoint, YieldsOnlyWhenSliceIsSpent) {
    g_now = {};
    tf::Scheduler::Config cfg; cfg.clock = &FakeNow; cfg.time_slice = std::chrono::milliseconds(2);
    int iterations = 0;
    auto s = tf::Scheduler::Create([&] {
        for (int i = 0; i < 1000; ++i) { ++iterations; if (i == 500) g_now += std::chrono::milliseconds(3); tf::CheckPoint(); }
    }, cfg);
    s->Step();                       // runs until the slice is exhausted at i==500 (plus stride slack)
    EXPECT_GE(iterations, 501);
    EXPECT_LT(iterations, 1000);
    while (s->Step()) {}
    EXPECT_EQ(iterations, 1000);
}

TEST(TinyFiberCheckPoint, RespectsRunForDeadline) { /* RunFor(1ms) with time_slice 1h still returns after ~1ms */ }

TEST(TinyFiberCheckPoint, ThrowsWhenStopping) { /* Stop(), then CheckPoint() in the fiber throws SchedulerStoppingError */ }

TEST(TinyFiberCheckPoint, SameFunctionRunsSyncAndCooperative) {
    auto work = [] { long sum = 0; for (int i = 0; i < 100000; ++i) { sum += i; tf::CheckPoint(); } return sum; };
    const long sync = work();
    EXPECT_EQ(tf::Scheduler::Run(work), sync);
}
```

- [ ] **Step 2: FAIL. Step 3: Implement. Step 4: PASS + benchmark (add `checkpoint_loop` bench: ns/op of CheckPoint in a tight loop). Step 5: Commit** (ask first).

### Task 13: Per-fiber cancellation

**Files:** Modify `future.hpp`, `scheduler.hpp/.cpp`, `detail/fiber.hpp`, `yield.hpp/.cpp`, `condition_variable.cpp`; Create `tests/tiny_fiber_cancel_test.cpp`.

**Interfaces (produces):** `void Future<T>::Cancel() noexcept;` `[[nodiscard]] bool IsCancellationRequested();` (yield.hpp). Cancellation points: `Yield`, `CheckPoint`, `SleepFor/Until`, `Future::Wait/Get/WaitFor`, `ConditionVariable::Wait*`, `Channel::Send/Receive`. `Mutex::Lock` is **not** a cancellation point (it stays stop-aware). A cancelled fiber whose Future is destroyed during unwinding cancels its own children before joining them.

- [ ] **Step 1: Failing tests**

```cpp
TEST(TinyFiberCancel, CancelWakesSleepingFiberWithCancelledError) {
    tf::Scheduler::Run([] {
        auto f = tf::Spawn([] { tf::SleepFor(std::chrono::hours(1)); return 1; });
        tf::Yield();
        f.Cancel();
        EXPECT_THROW((void)f.Get(), tf::CancelledError);
    });
}

TEST(TinyFiberCancel, CancelBeforeStartSkipsBody) { /* body flag stays false; Get throws CancelledError */ }

TEST(TinyFiberCancel, CancelIsObservedAtCheckPoint) { /* loop with CheckPoint exits via CancelledError */ }

TEST(TinyFiberCancel, MutexLockIsNotACancellationPoint) { /* cancelled waiter still acquires, throws at next Yield */ }

TEST(TinyFiberCancel, CancellingParentCancelsChildren) {
    bool child_cancelled = false;
    tf::Scheduler::Run([&] {
        auto parent = tf::Spawn([&] {
            auto child = tf::Spawn([&] {
                try { tf::SleepFor(std::chrono::hours(1)); } catch (const tf::CancelledError&) { child_cancelled = true; throw; }
            });
            tf::SleepFor(std::chrono::hours(1));
        });
        tf::Yield(); tf::Yield();
        parent.Cancel();
        EXPECT_THROW(parent.Wait(), tf::CancelledError);
    });
    EXPECT_TRUE(child_cancelled);
}

TEST(TinyFiberCancel, CancelFinishedFiberIsNoOp) { /* Get still returns the value */ }
```

- [ ] **Step 2: FAIL. Step 3: Implement** (`Fiber::cancel_requested_`; `Scheduler::CancelFiber(id)` sets the flag and `WakeFiber` if parked cancellably; `ThrowIfInterrupted(cancellable)`; ~Future join propagates cancel when the current fiber is cancelled). **Step 4: PASS. Step 5: Commit** (ask first).

### Task 14: WaitFor/WaitUntil, WaitAll/WaitAny, ConditionVariable timeouts

**Files:** Create `include/cortex/tiny_fiber/wait.hpp`; Modify `future.hpp`, `condition_variable.hpp/.cpp`; tests in `tests/tiny_fiber_future_test.cpp`.

**Interfaces (produces):**

```cpp
template <typename Rep, typename Period> bool Future<T>::WaitFor(std::chrono::duration<Rep, Period> d);
bool Future<T>::WaitUntil(Scheduler::TimePoint deadline);
template <typename... Fs> void WaitAll(Fs&... futures);
template <typename... Fs> std::size_t WaitAny(Fs&... futures);           // index of first ready
template <typename T> void WaitAll(std::span<Future<T>> futures);
template <typename T> std::size_t WaitAny(std::span<Future<T>> futures);
bool ConditionVariable::WaitFor(Mutex::Guard&, Duration);                  // false on timeout
template <typename P> bool ConditionVariable::WaitFor(Mutex::Guard&, Duration, P pred);
bool ConditionVariable::WaitUntil(Mutex::Guard&, TimePoint);
```

WaitAny registers one `WaiterRef` (same epoch) on every not-ready state and parks once; the first `MarkReady` wakes it; the rest are stale by epoch. `WaiterList::Push` prunes stale entries once it holds more than 4 (bounds growth when WaitAny is looped over long-lived futures).

- [ ] **Step 1: Failing tests**

```cpp
TEST(TinyFiberWait, WaitAnyReturnsFirstReadyIndex) {
    tf::Scheduler::Run([] {
        auto slow = tf::Spawn([] { tf::SleepFor(std::chrono::milliseconds(30)); return 1; });
        auto fast = tf::Spawn([] { tf::SleepFor(std::chrono::milliseconds(5)); return 2; });
        EXPECT_EQ(tf::WaitAny(slow, fast), 1u);
        EXPECT_EQ(fast.Get(), 2);
        EXPECT_EQ(slow.Get(), 1);
    });
}
TEST(TinyFiberWait, WaitAllWaitsForEvery) { /* span overload over vector<Future<int>> */ }
TEST(TinyFiberWait, FutureWaitForTimesOut) { /* returns false, then true after producer finishes */ }
TEST(TinyFiberWait, CvWaitForTimeoutLeavesNoStaleWake) {
    // Waiter times out of the CV, then parks on a Future. A later NotifyOne must not wake it.
}
TEST(TinyFiberWait, WaitAnyInLoopDoesNotGrowWithoutBound) { /* 10'000 iterations over the same long-lived future */ }
```

- [ ] **Step 2: FAIL. Step 3: Implement. Step 4: PASS. Step 5: Commit** (ask first).

### Task 15: `Channel<T>`

**Files:** Create `include/cortex/tiny_fiber/channel.hpp`, `tests/tiny_fiber_channel_test.cpp`.

**Interfaces (produces):**

```cpp
template <typename T>
class Channel {
public:
    static constexpr std::size_t kUnbounded = std::numeric_limits<std::size_t>::max();
    explicit Channel(std::size_t capacity = kUnbounded);     // capacity 0 → std::invalid_argument
    template <typename U> bool Send(U&& value);              // suspends while full; false if closed
    template <typename U> bool TrySend(U&& value);
    std::optional<T> Receive();                              // suspends while empty; nullopt when closed+drained
    std::optional<T> TryReceive();
    void Close();                                            // wakes all senders and receivers
    [[nodiscard]] bool IsClosed() const noexcept;
    [[nodiscard]] std::size_t Size() const noexcept;
    class Iterator; Iterator begin(); std::default_sentinel_t end() const noexcept;
};
```

- [ ] **Step 1: Failing tests**: producer/consumer ordering; bounded capacity blocks sender (observe interleaving); close drains then ends range-for; send-after-close returns false; cancel a blocked receiver → `CancelledError`; `StopWakesEveryKindOfWait` (sleep + channel + promise + WaitAny parked at once, then `Stop()` → `IsDone()`).
- [ ] **Step 2: FAIL. Step 3: Implement** (std::deque buffer + two `WaitQueue`s; Send/Receive loop re-checking state after each wake). **Step 4: PASS. Step 5: Commit** (ask first).

### Task 16: `Promise<T>`, `Post()`, wake-up handler

**Files:** Create `include/cortex/tiny_fiber/promise.hpp`; Modify `scheduler.hpp/.cpp`; tests in `tests/tiny_fiber_future_test.cpp`.

**Interfaces (produces):**

```cpp
template <typename T>
class Promise {
public:
    Promise();                                   // binds to Scheduler::Current()
    explicit Promise(Scheduler& scheduler);
    Promise(Promise&&) noexcept; Promise& operator=(Promise&&) noexcept;
    ~Promise();                                  // unfulfilled → BrokenPromiseError
    Future<T> GetFuture();                       // once
    template <typename U = T> void SetValue(U&& value);  // Promise<void>: SetValue()
    void SetException(std::exception_ptr ex);
    [[nodiscard]] bool IsFulfilled() const noexcept;
};

void Scheduler::Post(fu2::unique_function<void()> work);   // thread-safe; runs at next Step()
void Scheduler::SetWakeupHandler(std::function<void()> h); // called when work arrives outside Step()
```

Fibers parked on a promise-backed future count as *external* waits → `GetStatus()` reports `kWaiting` (not `kDeadlocked`); `Run()` blocks on `post_cv_` until posted work arrives.

- [ ] **Step 1: Failing tests**: fiber awaits promise fulfilled by a later `Step()`-external call (status `kWaiting` in between, wake-up handler invoked exactly once); destroyed promise → `BrokenPromiseError`; `SetValue` twice → `std::logic_error`; `Post` from a `std::thread` completes a promise while `Run()` blocks; promise fulfilled after the scheduler is destroyed → no crash.
- [ ] **Step 2: FAIL. Step 3: Implement. Step 4: PASS (+ TSan-free by construction: only `Post` touches the mutex). Step 5: Commit** (ask first).

---

# Phase 4 — Browser integration

### Task 17: WASM driver exports + `js/cortex.mjs`

**Files:** Modify `src/tiny_fiber/scheduler.cpp` (append `#ifdef __EMSCRIPTEN__` block); Create `js/cortex.mjs`, `tests/web/driver_test.cpp`, `tests/web/driver_test.mjs`; Modify `tests/CMakeLists.txt`.

**Interfaces (produces):**

```cpp
extern "C" {
CORTEX_API int cortex_scheduler_run_for(void* scheduler, double budget_ms); // Status, or 4 = failed
CORTEX_API double cortex_scheduler_next_timer_ms(void* scheduler);          // ms until next timer, -1 none
CORTEX_API const char* cortex_scheduler_describe(void* scheduler);
CORTEX_API const char* cortex_scheduler_last_error();
CORTEX_API void cortex_scheduler_attach(void* scheduler);                   // wake-up → Module.cortexWake(ptr)
}
```

```js
// js/cortex.mjs
export const Status = Object.freeze({ Runnable: 0, Waiting: 1, Done: 2, Deadlocked: 3, Failed: 4 });
export function drive(Module, scheduler, { budgetMs = 8 } = {}) → { done: Promise<void>, stop(): void }
```

Driver: runs `run_for(budgetMs)` per macrotask (MessageChannel, so input and rendering interleave), sleeps with `setTimeout(next_timer_ms)` while `Waiting`, is woken by `Module.cortexWake` when a promise is fulfilled from JS, resolves `done` on `Done`, rejects with the fiber description on `Deadlocked` and with the exception text on `Failed`.

The exports live in `scheduler.cpp` (not a separate object) because objects in a static archive are only linked when referenced.

- [ ] **Step 1: Failing node test** (`driver_test.mjs`, built module `cortex_web_driver_test.mjs` with `-sMODULARIZE -sEXPORT_ES6 -sENVIRONMENT=node`): (a) three 20 ms sleeps finish in ≥ 60 ms while a `setInterval(1)` counter keeps ticking; (b) a 150 ms busy loop with `CheckPoint()` lets the interval tick ≥ 20 times; (c) deadlock rejects with a message containing `deadlock`; (d) entry exception rejects with its `what()`.
- [ ] **Step 2: `./dev.sh test-wasm` → FAIL. Step 3: Implement. Step 4: PASS. Step 5: Commit** (ask first).

### Task 18: `cortex::web::Await(emscripten::val)`

**Files:** Create `include/cortex/web/await.hpp`, `src/web/await.cpp`; Modify `src/CMakeLists.txt` (`cortex_web` target, WASM only, links `-lembind`), tests in `tests/web/`.

**Interfaces (produces):**

```cpp
namespace cortex::web {
class JsError : public std::runtime_error { public: explicit JsError(emscripten::val reason); const emscripten::val& Reason() const noexcept; };
/// Suspends the calling fiber (not the page) until `promise` settles.
emscripten::val Await(emscripten::val promise);
}
```

Mechanism: heap-allocated `tf::Promise<val>` passed as context to an `EM_JS` helper that attaches `.then/.catch`; both call the exported `cortex_web_settle(ctx, ok, handle)`, which fulfills and frees it. Uses `EM_JS_DEPS(..., "$Emval")`.

- [ ] **Step 1: Failing node tests**: await a `setTimeout`-resolved promise (value 42); rejected promise → `JsError` with message; two fibers awaiting different promises complete in settle order; promise settling after `drive().stop()` + scheduler destruction → no crash.
- [ ] **Step 2: FAIL. Step 3: Implement. Step 4: `./dev.sh test-wasm` PASS. Step 5: Commit** (ask first).

### Task 19: Video editor proves it — delete the duplicated filters

**Files:** Modify `apps/video_editor/engine/src/filters/*.cpp` (add `cortex::tiny_fiber::CheckPoint()` per row), `apps/video_editor/engine/src/live_cooperative.cpp` (run the real `FilterChain` inside the fiber; delete the duplicated math), `live_cooperative.hpp` (drop `band_rows`), `bridge/wasm_bridge.cpp` (+ `editor_run_cooperative_for(double ms)`), `web/src/editor_client.js`, `web/src/playback.js` (replace the manual budget loop), `engine/tests/live_cooperative_test.cpp` (band-size params → time-slice params).

- [ ] **Step 1:** Adjust tests: cooperative output still byte-identical to sync for every filter combination; a render with `time_slice = 0` takes > 1 step (proves it yields).
- [ ] **Step 2: FAIL → Step 3: implement → Step 4:** native suite PASS; `./dev.sh video-editor` builds; `wc -l live_cooperative.cpp` shrinks from 244 to < 90. **Step 5: Commit** (ask first).

---

# Phase 5 — Docs & CI

### Task 20: README, porting guide, DEVELOPMENT

- README: pitch ("make C++ behave in the browser"), C++20, FetchContent/`find_package` snippets, correct JS example using `js/cortex.mjs`, feature table (Spawn/Future/Promise/Channel/WaitAny/SleepFor/CheckPoint/Cancel/Await), behavior-changes note.
- `docs/porting-guide.md`: "Un-freeze your Emscripten app in 15 minutes" — three recipes (blocking main loop, long computation, awaiting fetch) + comparison table (Asyncify alone / `emscripten_set_main_loop` / Web Workers + pthreads / Cortex).
- DEVELOPMENT.md: new options (`CORTEX_USE_SYSTEM_BOOST`, `CORTEX_INSTALL`, `CORTEX_BUILD_EXPERIMENTAL`, `CORTEX_WASM_ASYNCIFY_STACK_SIZE`), package checks, guard pages, "don't suspend inside a catch block" note.
- Verification: re-read for placeholders/contradictions; every code snippet compiles (copy into `tests/package/main.cpp` style scratch build).

### Task 21: CI

- `Dockerfile`: `libboost-context-dev` for the install check.
- `docker-compose.yml`: `test-package` service running both `tests/package/check_*.sh`.
- `ci.yml`: `test-package` job; keep benchmark regression job.
- Verification: `docker compose run --rm test-package` PASS locally; `actionlint`-style YAML sanity via `python3 -c 'import yaml,sys; yaml.safe_load(open(".github/workflows/ci.yml"))'`.

---

## Self-review

- Coverage: F1→T1/T2, F2→T3, F3→T1/T20, F4/F5→T7, F6→T4, F7→T6, F8→T5 (+T13/T14 tests), F9→T8, F10→T10, F11→T9, F12→T17/T20, F13→T11–T19. ✔
- Names used across tasks: `WaiterRef`, `WaitQueue`, `WaiterList`, `PrepareWait`, `ParkCurrent`, `WakeIfWaiting`, `WakeFiber`, `ThrowIfInterrupted`, `MakeFutureState`, `MakeSpawnBody`, `Status::{kRunnable,kWaiting,kDone,kDeadlocked}`, `RunFor/RunUntil/NextTimerDeadline`, `CheckPoint`, `CancelledError`, `BrokenPromiseError`, `DeadlockError`, `Promise`, `Post`, `SetWakeupHandler` — consistent. ✔
- Review Focus items each have an owning test (T6, T18, T15, T13/T14, T12). ✔
