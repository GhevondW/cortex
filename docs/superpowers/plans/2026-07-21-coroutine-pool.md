# Coroutine Pool Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Pool whole coroutines (live context + stack) so acquiring one costs a free-list pop instead of context creation, with a public `CoroutinePool`/`LocalCoroutinePool` API and transparent fiber reuse inside `tiny_fiber::Scheduler`.

**Architecture:** `detail::CoroutineImpl` (both backends) gains a reusable "trampoline" mode: the entry function loops `run body → park → wait for rebind`, so the Boost.Context/Asyncify context never dies between uses. A non-template `detail::CoroutinePoolState` holds the free list; thin `BasicCoroutinePool<bool ThreadSafe>` / `BasicPooledCoroutine<bool ThreadSafe>` templates add the locking policy (`std::mutex` vs no-op `NullMutex`) and are explicitly instantiated in one `.cpp`. The Scheduler reuses finished `Fiber` objects directly via a body-less rebind (no mutex, single-threaded).

**Tech Stack:** C++23, Boost.Context (native) / Emscripten Asyncify (WASM), function2, GoogleTest, CMake. Spec: `docs/superpowers/specs/2026-07-21-coroutine-pool-design.md`.

---

## Environment notes (read first)

- Repo: `/Users/ghevond/Projects/cortex`. All commands run from the repo root.
- **Git is hook-blocked on this machine** — prefix every git command with `AISUITE_ALLOW_GIT=1`, e.g. `AISUITE_ALLOW_GIT=1 git add -A`. Standalone `grep`/`find` are also blocked; piping (`cmd | grep`) is allowed, and use Glob/Read tools for file discovery.
- **Fast native loop (local, macOS arm64):**
  ```bash
  cmake -B build/native-release -DCMAKE_BUILD_TYPE=Release -DCORTEX_BUILD_TESTS=ON
  cmake --build build/native-release -j
  ctest --test-dir build/native-release --output-on-failure
  ```
  The `build/native-release` directory already exists and is configured.
- **Authoritative runs (Docker, exit code propagates):**
  ```bash
  docker compose run --rm test-native   # Linux clang, all native tests
  docker compose run --rm test-wasm    # Emscripten + node, all WASM tests
  CORTEX_USE_SANITIZERS=ON docker compose run --rm test-native   # Linux ASan/UBSan
  ```
  Do NOT build WASM or ASan locally on the host: host macOS ASan is broken (illegal instruction) and host `emcc` picks up a broken Python.
- Formatting: `./format` (clang-format wrapper). Run before the final commit.
- Commit messages end with `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.

## File map

| File | Action | Responsibility |
|---|---|---|
| `include/cortex/detail/null_mutex.hpp` | Create | No-op Lockable for single-threaded pools |
| `src/detail/coroutine_native_impl.hpp/.cpp` | Modify | Trampoline mode, `Rebind`, `AbortBody` (Boost.Context) |
| `src/detail/coroutine_emscripten_impl.hpp/.cpp` | Modify | Same for Asyncify |
| `include/cortex/coroutine_pool.hpp` | Create | Public pool + handle API (templates over locking policy) |
| `src/coroutine_pool.cpp` | Create | `CoroutinePoolState` + all template member defs + explicit instantiation |
| `include/cortex/coroutine.hpp` + `src/coroutine.cpp` | Modify | Private `MakeInternal(reusable)` / `RebindForReuseInternal()` for BaseCoroutine |
| `include/cortex/base_coroutine.hpp` + `src/base_coroutine.cpp` | Modify | Reusable ctor flag, `ResetCoroutineForReuse()`, `GetStackSize()` |
| `include/cortex/tiny_fiber/detail/fiber.hpp` + `src/tiny_fiber/fiber.cpp` | Modify | `reusable` ctor param, `ResetForReuse(id, body)` |
| `include/cortex/tiny_fiber/scheduler.hpp` + `src/tiny_fiber/scheduler.cpp` | Modify | `max_pooled_fibers` config, `free_fibers_` list, warm spawn path |
| `src/CMakeLists.txt` | Modify | Add `coroutine_pool.cpp` |
| `tests/coroutine_pool_test.cpp` | Create | Pool + handle unit tests (native + WASM) |
| `tests/CMakeLists.txt` | Modify | Register new test (native + WASM blocks) |
| `tests/base_coroutine_test.cpp` | Modify | Reusable-subclass test |
| `tests/tiny_fiber_test.cpp` | Modify | Fiber-reuse tests |
| `benchmarks/bench_main.cpp` | Modify | `pool_acquire_release`, `pool_rebind_resume` benches |
| `DEVELOPMENT.md` | Modify | Performance section note |

---

### Task 1: Reusable trampoline mode — native backend + NullMutex

**Files:**
- Create: `include/cortex/detail/null_mutex.hpp`
- Modify: `src/detail/coroutine_native_impl.hpp`
- Modify: `src/detail/coroutine_native_impl.cpp`

This is a refactor guarded by the existing native test suite: `reusable` defaults to `false`, and a non-reusable coroutine behaves exactly as before (the trampoline loop breaks after the first body).

- [ ] **Step 1: Create `include/cortex/detail/null_mutex.hpp`**

```cpp
#pragma once

/**
 * @file null_mutex.hpp
 * @brief No-op mutex for single-threaded pool instantiations.
 */

namespace cortex::detail {

/**
 * @struct NullMutex
 * @brief Satisfies the Lockable concept with no-ops.
 *
 * Used via std::conditional_t to compile the locking out of
 * single-threaded pool instantiations entirely.
 */
struct NullMutex {
    void lock() noexcept {}
    void unlock() noexcept {}
    bool try_lock() noexcept {
        return true;
    }
};

} // namespace cortex::detail
```

- [ ] **Step 2: Replace `src/detail/coroutine_native_impl.hpp` with:**

```cpp
#pragma once

#include <cstddef>
#include <exception>

#include <boost/context/fiber.hpp>

#include <cortex/coroutine_body.hpp>
#include <cortex/memory_resource.hpp>

namespace cortex::detail {

class CoroutineImpl final {
public:
    CoroutineImpl(cortex::CoroutineBody body,
                  std::size_t stack_size,
                  const MemoryResourceSharedPtr& resource,
                  bool reusable = false);
    ~CoroutineImpl();

    [[nodiscard]] std::size_t GetStackSize() const noexcept;
    [[nodiscard]] bool IsDone() const noexcept;
    [[nodiscard]] bool HasException() const noexcept;
    [[nodiscard]] bool IsUnwinding() const noexcept;
    [[nodiscard]] bool ShouldAbortBody() const noexcept;
    void Resume();

    // Reuse API. Only valid when constructed with reusable == true: a
    // reusable coroutine parks after its body finishes instead of letting
    // the context die.

    // Replace the body and arm the coroutine for another run. Valid when the
    // previous body finished or the coroutine never started.
    // @throws std::logic_error if a started body has not finished.
    void Rebind(cortex::CoroutineBody body);

    // Re-arm with the current body. Used by BaseCoroutine reuse, where the
    // body is always `[this](ctx) { Continuation(ctx); }`.
    // @throws std::logic_error if a started body has not finished.
    void Rebind();

    // Force-unwind a started-but-unfinished body and park the trampoline so
    // the coroutine can be rebound. No-op if the body already finished or
    // never started.
    void AbortBody();

private:
    bool is_done_ {false};
    bool is_unwinding_ {false};
    bool abort_body_ {false};
    bool reusable_ {false};
    // The context entered its entry function at least once (destructor must
    // not resume a never-started context — that would run the body).
    bool started_ {false};
    // The CURRENT body began executing. Distinct from started_: a parked
    // coroutine that was rebound has started_ == true but body_started_ ==
    // false until the next Resume.
    bool body_started_ {false};
    std::size_t stack_size_bytes_;
    cortex::CoroutineBody body_;
    std::exception_ptr exception_ptr_ {nullptr};
    boost::context::fiber fiber_;
};

} // namespace cortex::detail
```

- [ ] **Step 3: Replace `src/detail/coroutine_native_impl.cpp` with:**

```cpp
#include "detail/coroutine_native_impl.hpp"

#include <cassert>
#include <stdexcept>
#include <utility>

#include "cortex/coroutine_suspend_context.hpp"
#include "cortex/errors/resume_on_completed_coroutine_error.hpp"
#include <cortex/detail/forced_unwind.hpp>

namespace cortex::detail {

namespace {

struct FiberSuspendContext final : cortex::CoroutineSuspendContext {
    explicit FiberSuspendContext(boost::context::fiber& sink, CoroutineImpl* impl)
        : _sink(sink)
        , _impl(impl) {}

    ~FiberSuspendContext() override = default;

    void Suspend() override {
        _sink = std::move(_sink).resume();
        if (_impl->IsUnwinding() || _impl->ShouldAbortBody()) {
            throw ForcedUnwind {};
        }
    }

private:
    boost::context::fiber& _sink;
    CoroutineImpl* _impl;
};

struct MemoryResourceStackAllocator {
    MemoryResourceSharedPtr resource;
    std::size_t size;

    boost::context::stack_context allocate() {
        void* vp = resource->Allocate(size);
        boost::context::stack_context sctx;
        sctx.size = size;
        sctx.sp = static_cast<char*>(vp) + sctx.size;
        return sctx;
    }

    void deallocate(boost::context::stack_context& sctx) {
        resource->Deallocate(static_cast<char*>(sctx.sp) - sctx.size, sctx.size);
    }
};

} // namespace

CoroutineImpl::CoroutineImpl(cortex::CoroutineBody body,
                             std::size_t stack_size,
                             const MemoryResourceSharedPtr& resource,
                             bool reusable)
    : reusable_(reusable)
    , stack_size_bytes_(stack_size)
    , body_(std::move(body))
    , fiber_(std::allocator_arg,
             MemoryResourceStackAllocator {resource, stack_size},
             [this](boost::context::fiber&& sink) {
                 started_ = true;
                 FiberSuspendContext suspend_context(sink, this);

                 for (;;) {
                     assert(static_cast<bool>(body_));
                     body_started_ = true;
                     try {
                         body_(suspend_context);
                     } catch (const ForcedUnwind&) {
                         // Unwinding in progress or the body is being aborted
                     } catch (...) {
                         assert(!static_cast<bool>(exception_ptr_));
                         exception_ptr_ = std::current_exception();
                     }

                     is_done_ = true;

                     if (!reusable_) {
                         // Release the body's captures at completion, matching
                         // the pre-trampoline body lifetime.
                         body_ = cortex::CoroutineBody {};
                         break;
                     }
                     if (is_unwinding_) {
                         break;
                     }

                     // Park: give control back and wait for Rebind() +
                     // Resume(), or teardown.
                     sink = std::move(sink).resume();

                     if (is_unwinding_) {
                         break;
                     }
                 }

                 return std::move(sink);
             }) {
    assert(stack_size_bytes_ > 0);
}

CoroutineImpl::~CoroutineImpl() {
    // A started context (mid-body or parked) must be resumed with the
    // unwinding flag set so the trampoline exits and the stack is released.
    // A never-started context must NOT be resumed — that would execute the
    // body during destruction; boost's ~fiber() unwinds it without entering
    // the entry function. A finished one-shot leaves fiber_ empty.
    if (fiber_ && started_) {
        is_unwinding_ = true;
        fiber_ = std::move(fiber_).resume();
    }
}

std::size_t CoroutineImpl::GetStackSize() const noexcept {
    return stack_size_bytes_;
}

bool CoroutineImpl::IsDone() const noexcept {
    return is_done_;
}

bool CoroutineImpl::HasException() const noexcept {
    return static_cast<bool>(exception_ptr_);
}

bool CoroutineImpl::IsUnwinding() const noexcept {
    return is_unwinding_;
}

bool CoroutineImpl::ShouldAbortBody() const noexcept {
    return abort_body_;
}

void CoroutineImpl::Resume() {
    if (IsDone()) {
        throw ResumeOnDoneCoroutineError {"Resume on finished coroutine."};
    }

    fiber_ = std::move(fiber_).resume();

    if (exception_ptr_) {
        auto ex = exception_ptr_;
        exception_ptr_ = nullptr;
        std::rethrow_exception(ex);
    }
}

void CoroutineImpl::Rebind(cortex::CoroutineBody body) {
    assert(reusable_);
    if (body_started_ && !is_done_) {
        throw std::logic_error("Rebind on a coroutine whose body has not finished.");
    }
    body_ = std::move(body);
    // Discard any leftover exception from an aborted body: its outcome is
    // dropped by definition and must not leak into the next run.
    exception_ptr_ = nullptr;
    body_started_ = false;
    is_done_ = false;
}

void CoroutineImpl::Rebind() {
    assert(reusable_);
    if (body_started_ && !is_done_) {
        throw std::logic_error("Rebind on a coroutine whose body has not finished.");
    }
    // Discard any leftover exception from an aborted body: its outcome is
    // dropped by definition and must not leak into the next run.
    exception_ptr_ = nullptr;
    body_started_ = false;
    is_done_ = false;
}

void CoroutineImpl::AbortBody() {
    assert(reusable_);
    if (is_done_ || !body_started_) {
        return;
    }
    abort_body_ = true;
    fiber_ = std::move(fiber_).resume();
    abort_body_ = false;
}

} // namespace cortex::detail
```

- [ ] **Step 4: Build and run the full native suite (regression gate)**

Run: `cmake --build build/native-release -j && ctest --test-dir build/native-release --output-on-failure`
Expected: 100% tests pass (no behavior change for non-reusable coroutines).

- [ ] **Step 5: Commit**

```bash
AISUITE_ALLOW_GIT=1 git add include/cortex/detail/null_mutex.hpp src/detail/coroutine_native_impl.hpp src/detail/coroutine_native_impl.cpp
AISUITE_ALLOW_GIT=1 git commit -m "feat: reusable trampoline mode in native coroutine impl

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 2: Reusable trampoline mode — Emscripten backend

**Files:**
- Modify: `src/detail/coroutine_emscripten_impl.hpp`
- Modify: `src/detail/coroutine_emscripten_impl.cpp`

Same semantics as Task 1. Key differences: the body is already a member; `Resume` is split into a guard + a raw `SwapIn()` so the destructor can unwind a parked (done) trampoline without tripping the `IsDone` guard.

- [ ] **Step 1: Replace `src/detail/coroutine_emscripten_impl.hpp` with:**

```cpp
#pragma once

#include <cstddef>
#include <exception>

#include <emscripten/fiber.h>

#include <cortex/coroutine_body.hpp>
#include <cortex/memory_resource.hpp>

namespace cortex::detail {

class CoroutineImpl final {
public:
    CoroutineImpl(cortex::CoroutineBody body,
                  std::size_t stack_size,
                  const MemoryResourceSharedPtr& resource,
                  bool reusable = false);
    ~CoroutineImpl();

    [[nodiscard]] std::size_t GetStackSize() const noexcept;
    [[nodiscard]] bool IsDone() const noexcept;
    [[nodiscard]] bool HasException() const noexcept;
    [[nodiscard]] bool IsUnwinding() const noexcept;
    [[nodiscard]] bool ShouldAbortBody() const noexcept;
    [[nodiscard]] emscripten_fiber_t* GetBackFiber() const noexcept;
    void SetBackFiber(emscripten_fiber_t* fiber) noexcept;
    void Resume();

    // Reuse API — see coroutine_native_impl.hpp for the contract.
    void Rebind(cortex::CoroutineBody body);
    void Rebind();
    void AbortBody();

private:
    static void FiberEntry(void* arg);

    // Raw swap into this fiber, bypassing the IsDone guard and exception
    // rethrow. Shared by Resume, AbortBody and the unwinding destructor.
    void SwapIn();

    emscripten_fiber_t fiber_;
    emscripten_fiber_t* back_fiber_ {nullptr};
    cortex::CoroutineBody body_;
    bool is_done_ {false};
    bool is_unwinding_ {false};
    bool abort_body_ {false};
    bool reusable_ {false};
    // The context entered its entry function at least once (destructor must
    // not resume a never-started context — that would run the body).
    bool started_ {false};
    // The CURRENT body began executing. Distinct from started_: a parked
    // coroutine that was rebound has started_ == true but body_started_ ==
    // false until the next Resume.
    bool body_started_ {false};
    std::size_t stack_size_bytes_;
    std::exception_ptr exception_ptr_ {nullptr};
    MemoryResourceSharedPtr resource_;
    void* c_stack_ {nullptr};
    void* asyncify_stack_ {nullptr};
};

} // namespace cortex::detail
```

- [ ] **Step 2: Replace `src/detail/coroutine_emscripten_impl.cpp` with:**

```cpp
#include "detail/coroutine_emscripten_impl.hpp"

#include <cassert>
#include <cstdlib>
#include <emscripten.h>
#include <stdexcept>
#include <utility>

#include "cortex/coroutine_suspend_context.hpp"
#include "cortex/errors/resume_on_completed_coroutine_error.hpp"
#include "cortex/memory_resource.hpp"
#include <cortex/detail/forced_unwind.hpp>

namespace cortex::detail {

namespace {

static constexpr std::size_t kAsyncifyStackSize = 16384;
static constexpr std::size_t kStackAlignment = 16;

// Track the currently executing fiber
thread_local emscripten_fiber_t* running_fiber = nullptr;

// Persistent state for the "main" fiber (the one calling Resume from JS)
struct MainFiberContext {
    emscripten_fiber_t fiber;
    void* asyncify_stack {nullptr};
    cortex::MemoryResourceSharedPtr resource;

    MainFiberContext()
        : resource(cortex::GetDefaultMemoryResource()) {
        asyncify_stack = resource->Allocate(kAsyncifyStackSize, kStackAlignment);
    }
    ~MainFiberContext() {
        if (asyncify_stack) {
            resource->Deallocate(asyncify_stack, kAsyncifyStackSize, kStackAlignment);
        }
    }
};

MainFiberContext& GetMainContext() {
    thread_local MainFiberContext instance;
    return instance;
}

struct FiberSuspendContext final : cortex::CoroutineSuspendContext {
    explicit FiberSuspendContext(CoroutineImpl* impl)
        : impl_(impl) {}

    ~FiberSuspendContext() override = default;

    void Suspend() override {
        emscripten_fiber_t* back_f = impl_->GetBackFiber();
        emscripten_fiber_t* current_f = running_fiber;

        running_fiber = back_f;

        emscripten_fiber_swap(current_f, back_f);

        if (impl_->IsUnwinding() || impl_->ShouldAbortBody()) {
            throw ForcedUnwind {};
        }
    }

private:
    CoroutineImpl* impl_;
};

} // namespace

CoroutineImpl::CoroutineImpl(cortex::CoroutineBody body,
                             std::size_t stack_size,
                             const MemoryResourceSharedPtr& resource,
                             bool reusable)
    : body_(std::move(body))
    , reusable_(reusable)
    , stack_size_bytes_(stack_size)
    , resource_(resource) {
    if (emscripten_has_asyncify() != 1) {
        throw std::runtime_error("Cortex requires ASYNCIFY to be enabled for Emscripten.");
    }

    // Neither stack needs zero-initialization: emscripten_fiber_init writes
    // the asyncify bookkeeping itself, and the C stack contents are written
    // before use. Zeroing them cost a 256KB+16KB memset per coroutine.
    try {
        c_stack_ = resource_->Allocate(stack_size, kStackAlignment);
        asyncify_stack_ = resource_->Allocate(kAsyncifyStackSize, kStackAlignment);
    } catch (...) {
        if (c_stack_) resource_->Deallocate(c_stack_, stack_size, kStackAlignment);
        throw;
    }

    emscripten_fiber_init(&fiber_, FiberEntry, this, c_stack_, stack_size, asyncify_stack_, kAsyncifyStackSize);
}

CoroutineImpl::~CoroutineImpl() {
    // A started context (mid-body or parked) must be unwound so the
    // trampoline exits. A never-started context must NOT be swapped in —
    // that would execute the body during destruction; the fiber never ran,
    // so freeing the stacks below is all the cleanup it needs. A finished
    // one-shot is already dead.
    const bool context_alive = started_ && (reusable_ || !is_done_);
    if (context_alive) {
        is_unwinding_ = true;
        SwapIn();
    }

    if (c_stack_) resource_->Deallocate(c_stack_, stack_size_bytes_, kStackAlignment);
    if (asyncify_stack_) resource_->Deallocate(asyncify_stack_, kAsyncifyStackSize, kStackAlignment);
}

void CoroutineImpl::FiberEntry(void* arg) {
    auto* self = static_cast<CoroutineImpl*>(arg);
    assert(self);
    self->started_ = true;

    FiberSuspendContext suspend_context(self);

    for (;;) {
        assert(static_cast<bool>(self->body_));
        self->body_started_ = true;
        try {
            self->body_(suspend_context);
        } catch (const ForcedUnwind&) {
            // Unwinding in progress or the body is being aborted
        } catch (...) {
            assert(!static_cast<bool>(self->exception_ptr_));
            self->exception_ptr_ = std::current_exception();
        }

        self->is_done_ = true;

        if (!self->reusable_) {
            // Release the body's captures at completion, matching the
            // pre-trampoline body lifetime.
            self->body_ = cortex::CoroutineBody {};
            break;
        }
        if (self->is_unwinding_) {
            break;
        }

        // Park: swap back and wait for Rebind() + Resume(), or teardown.
        emscripten_fiber_t* park_back_f = self->back_fiber_;
        running_fiber = park_back_f;
        emscripten_fiber_swap(&self->fiber_, park_back_f);

        if (self->is_unwinding_) {
            break;
        }
    }

    // Exit fiber back to the context that resumed us
    emscripten_fiber_t* back_f = self->back_fiber_;
    running_fiber = back_f;
    emscripten_fiber_swap(&self->fiber_, back_f);
}

std::size_t CoroutineImpl::GetStackSize() const noexcept {
    return stack_size_bytes_;
}

bool CoroutineImpl::IsDone() const noexcept {
    return is_done_;
}

bool CoroutineImpl::HasException() const noexcept {
    return static_cast<bool>(exception_ptr_);
}

bool CoroutineImpl::IsUnwinding() const noexcept {
    return is_unwinding_;
}

bool CoroutineImpl::ShouldAbortBody() const noexcept {
    return abort_body_;
}

emscripten_fiber_t* CoroutineImpl::GetBackFiber() const noexcept {
    return back_fiber_;
}

void CoroutineImpl::SetBackFiber(emscripten_fiber_t* fiber) noexcept {
    back_fiber_ = fiber;
}

void CoroutineImpl::SwapIn() {
    emscripten_fiber_t* back_f = running_fiber;
    if (!back_f) {
        // Capture current JS call stack into main fiber context if not already in a fiber
        back_f = &GetMainContext().fiber;
        emscripten_fiber_init_from_current_context(back_f, GetMainContext().asyncify_stack, kAsyncifyStackSize);
    }

    back_fiber_ = back_f;
    running_fiber = &fiber_;

    emscripten_fiber_swap(back_f, &fiber_);

    // Upon return back here, the running fiber is what it was before
    running_fiber = back_f;
}

void CoroutineImpl::Resume() {
    if (IsDone()) {
        throw ResumeOnDoneCoroutineError {"Resume on finished coroutine."};
    }

    SwapIn();

    if (exception_ptr_) {
        auto ex = exception_ptr_;
        exception_ptr_ = nullptr;
        std::rethrow_exception(ex);
    }
}

void CoroutineImpl::Rebind(cortex::CoroutineBody body) {
    assert(reusable_);
    if (body_started_ && !is_done_) {
        throw std::logic_error("Rebind on a coroutine whose body has not finished.");
    }
    body_ = std::move(body);
    // Discard any leftover exception from an aborted body: its outcome is
    // dropped by definition and must not leak into the next run.
    exception_ptr_ = nullptr;
    body_started_ = false;
    is_done_ = false;
}

void CoroutineImpl::Rebind() {
    assert(reusable_);
    if (body_started_ && !is_done_) {
        throw std::logic_error("Rebind on a coroutine whose body has not finished.");
    }
    // Discard any leftover exception from an aborted body: its outcome is
    // dropped by definition and must not leak into the next run.
    exception_ptr_ = nullptr;
    body_started_ = false;
    is_done_ = false;
}

void CoroutineImpl::AbortBody() {
    assert(reusable_);
    if (is_done_ || !body_started_) {
        return;
    }
    abort_body_ = true;
    SwapIn();
    abort_body_ = false;
}

} // namespace cortex::detail
```

- [ ] **Step 2a: Note the member-init-order requirement**

The constructor initializer list must follow declaration order (`body_`, then `reusable_`, then `stack_size_bytes_`, then `resource_`) or `-Wreorder` fails the build with warnings-as-errors.

- [ ] **Step 3: Run the WASM suite (regression gate)**

Run: `docker compose run --rm test-wasm`
Expected: 100% tests pass. (This also compiles the file — do not skip.)

- [ ] **Step 4: Commit**

```bash
AISUITE_ALLOW_GIT=1 git add src/detail/coroutine_emscripten_impl.hpp src/detail/coroutine_emscripten_impl.cpp
AISUITE_ALLOW_GIT=1 git commit -m "feat: reusable trampoline mode in emscripten coroutine impl

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 3: Public pool API — tests first, then implementation

**Files:**
- Test: `tests/coroutine_pool_test.cpp` (create)
- Modify: `tests/CMakeLists.txt`
- Create: `include/cortex/coroutine_pool.hpp`
- Create: `src/coroutine_pool.cpp`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Write the failing tests — create `tests/coroutine_pool_test.cpp`:**

```cpp
#include <cortex/config.hpp>
#include <cortex/coroutine_pool.hpp>
#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#ifndef CORTEX_EMSCRIPTEN
#include <atomic>
#include <thread>
#endif

namespace {

class TrackingResource : public cortex::MemoryResource {
public:
    size_t allocations = 0;
    size_t deallocations = 0;

protected:
    void* DoAllocate(std::size_t bytes, std::size_t alignment) override {
        allocations++;
        return cortex::GetDefaultMemoryResource()->Allocate(bytes, alignment);
    }

    void DoDeallocate(void* p, std::size_t bytes, std::size_t alignment) override {
        deallocations++;
        cortex::GetDefaultMemoryResource()->Deallocate(p, bytes, alignment);
    }
};

} // namespace

TEST(CoroutinePoolTest, AcquireRunsBody) {
    cortex::LocalCoroutinePool pool;
    int runs = 0;
    auto coroutine = pool.Acquire([&runs](cortex::CoroutineSuspendContext&) {
        ++runs;
    });
    EXPECT_FALSE(coroutine.IsDone());
    coroutine.Resume();
    EXPECT_TRUE(coroutine.IsDone());
    EXPECT_EQ(runs, 1);
}

TEST(CoroutinePoolTest, SuspendAndResume) {
    cortex::LocalCoroutinePool pool;
    std::vector<int> sequence;
    auto coroutine = pool.Acquire([&sequence](cortex::CoroutineSuspendContext& ctx) {
        sequence.push_back(1);
        ctx.Suspend();
        sequence.push_back(2);
    });
    coroutine.Resume();
    EXPECT_EQ(sequence, (std::vector<int> {1}));
    coroutine.Resume();
    EXPECT_EQ(sequence, (std::vector<int> {1, 2}));
    EXPECT_TRUE(coroutine.IsDone());
}

TEST(CoroutinePoolTest, ReleaseParksAndAcquireReuses) {
    auto tracker = std::make_shared<TrackingResource>();
    cortex::LocalCoroutinePool pool({.memory_resource = tracker});

    {
        auto coroutine = pool.Acquire([](cortex::CoroutineSuspendContext&) {});
        coroutine.Resume();
    } // handle destruction releases to the pool
    EXPECT_EQ(pool.GetParkedCount(), 1u);

    const auto allocations_after_first = tracker->allocations;
    {
        auto coroutine = pool.Acquire([](cortex::CoroutineSuspendContext&) {});
        coroutine.Resume();
    }
    EXPECT_EQ(tracker->allocations, allocations_after_first);
    EXPECT_EQ(tracker->deallocations, 0u);
    EXPECT_EQ(pool.GetParkedCount(), 1u);
}

TEST(CoroutinePoolTest, RebindRunsNewBodyOnSameCoroutine) {
    cortex::LocalCoroutinePool pool;
    int first = 0;
    int second = 0;
    auto coroutine = pool.Acquire([&first](cortex::CoroutineSuspendContext&) {
        first = 1;
    });
    coroutine.Resume();
    EXPECT_TRUE(coroutine.IsDone());

    coroutine.Rebind([&second](cortex::CoroutineSuspendContext&) {
        second = 2;
    });
    EXPECT_FALSE(coroutine.IsDone());
    coroutine.Resume();
    EXPECT_TRUE(coroutine.IsDone());
    EXPECT_EQ(first, 1);
    EXPECT_EQ(second, 2);
}

TEST(CoroutinePoolTest, RebindBeforeFirstResumeReplacesBody) {
    cortex::LocalCoroutinePool pool;
    bool original_ran = false;
    bool replacement_ran = false;
    auto coroutine = pool.Acquire([&original_ran](cortex::CoroutineSuspendContext&) {
        original_ran = true;
    });
    coroutine.Rebind([&replacement_ran](cortex::CoroutineSuspendContext&) {
        replacement_ran = true;
    });
    coroutine.Resume();
    EXPECT_FALSE(original_ran);
    EXPECT_TRUE(replacement_ran);
}

TEST(CoroutinePoolTest, RebindThrowsWhileBodyIsSuspended) {
    cortex::LocalCoroutinePool pool;
    auto coroutine = pool.Acquire([](cortex::CoroutineSuspendContext& ctx) {
        ctx.Suspend();
    });
    coroutine.Resume(); // suspends inside the body
    EXPECT_FALSE(coroutine.IsDone());
    EXPECT_THROW(coroutine.Rebind([](cortex::CoroutineSuspendContext&) {}), std::logic_error);
    coroutine.Resume(); // let it finish
    EXPECT_TRUE(coroutine.IsDone());
}

TEST(CoroutinePoolTest, ExceptionPropagatesAndCoroutineStaysReusable) {
    cortex::LocalCoroutinePool pool;
    auto coroutine = pool.Acquire([](cortex::CoroutineSuspendContext&) {
        throw std::runtime_error("boom");
    });
    EXPECT_THROW(coroutine.Resume(), std::runtime_error);
    EXPECT_TRUE(coroutine.IsDone());

    bool ran = false;
    coroutine.Rebind([&ran](cortex::CoroutineSuspendContext&) {
        ran = true;
    });
    coroutine.Resume();
    EXPECT_TRUE(ran);
}

TEST(CoroutinePoolTest, ReleaseUnfinishedBodyUnwindsStack) {
    cortex::LocalCoroutinePool pool;
    bool destroyed = false;
    struct Sentinel {
        bool* flag;
        ~Sentinel() {
            *flag = true;
        }
    };
    {
        auto coroutine = pool.Acquire([&destroyed](cortex::CoroutineSuspendContext& ctx) {
            Sentinel sentinel {&destroyed};
            ctx.Suspend();
            ctx.Suspend();
        });
        coroutine.Resume();
        EXPECT_FALSE(destroyed);
    } // released mid-body: unwinds, then parks
    EXPECT_TRUE(destroyed);
    EXPECT_EQ(pool.GetParkedCount(), 1u);

    bool ran = false;
    auto coroutine = pool.Acquire([&ran](cortex::CoroutineSuspendContext&) {
        ran = true;
    });
    coroutine.Resume();
    EXPECT_TRUE(ran);
}

TEST(CoroutinePoolTest, MaxParkedEvictsExcessCoroutines) {
    auto tracker = std::make_shared<TrackingResource>();
    cortex::LocalCoroutinePool pool({.max_parked = 1, .memory_resource = tracker});

    auto a = pool.Acquire([](cortex::CoroutineSuspendContext&) {});
    auto b = pool.Acquire([](cortex::CoroutineSuspendContext&) {});
    a.Resume();
    b.Resume();
    a.Release();
    b.Release(); // over the cap: destroyed

    EXPECT_EQ(pool.GetParkedCount(), 1u);
    EXPECT_GT(tracker->deallocations, 0u);
}

TEST(CoroutinePoolTest, ReservePrewarmsWithoutRunning) {
    auto tracker = std::make_shared<TrackingResource>();
    cortex::LocalCoroutinePool pool({.memory_resource = tracker});
    pool.Reserve(4);
    EXPECT_EQ(pool.GetParkedCount(), 4u);

    const auto allocations_after_reserve = tracker->allocations;
    for (int i = 0; i < 4; ++i) {
        auto coroutine = pool.Acquire([](cortex::CoroutineSuspendContext&) {});
        coroutine.Resume();
    }
    EXPECT_EQ(tracker->allocations, allocations_after_reserve);
}

TEST(CoroutinePoolTest, ReserveIsCappedByMaxParked) {
    cortex::LocalCoroutinePool pool({.max_parked = 2});
    pool.Reserve(8);
    EXPECT_EQ(pool.GetParkedCount(), 2u);
}

TEST(CoroutinePoolTest, HandleOutlivesPool) {
    auto tracker = std::make_shared<TrackingResource>();
    std::optional<cortex::LocalPooledCoroutine> handle;
    {
        cortex::LocalCoroutinePool pool({.memory_resource = tracker});
        handle.emplace(pool.Acquire([](cortex::CoroutineSuspendContext& ctx) {
            ctx.Suspend();
        }));
        handle->Resume();
    } // pool destroyed; handle still owns its coroutine
    handle->Resume();
    EXPECT_TRUE(handle->IsDone());
    handle.reset(); // destroys the coroutine instead of parking
    EXPECT_EQ(tracker->allocations, tracker->deallocations);
}

TEST(CoroutinePoolTest, PoolDestructorReleasesParkedCoroutines) {
    auto tracker = std::make_shared<TrackingResource>();
    {
        cortex::LocalCoroutinePool pool({.memory_resource = tracker});
        pool.Reserve(3);
        auto coroutine = pool.Acquire([](cortex::CoroutineSuspendContext&) {});
        coroutine.Resume();
    }
    EXPECT_EQ(tracker->allocations, tracker->deallocations);
}

TEST(CoroutinePoolTest, GetStackSizeReportsConfiguredSize) {
    cortex::LocalCoroutinePool pool({.stack_size_bytes = 128 * 1024});
    auto coroutine = pool.Acquire([](cortex::CoroutineSuspendContext&) {});
    EXPECT_EQ(coroutine.GetStackSize(), 128u * 1024u);
}

TEST(CoroutinePoolTest, DestroyingNeverResumedCoroutineDoesNotRunBody) {
    bool ran = false;
    {
        cortex::LocalCoroutinePool pool;
        auto coroutine = pool.Acquire([&ran](cortex::CoroutineSuspendContext& ctx) {
            ran = true;
            ctx.Suspend();
        });
    } // released without Resume, then pool destroyed
    EXPECT_FALSE(ran);
}

TEST(CoroutinePoolTest, WarmReleaseWithoutResumeDoesNotRunBody) {
    cortex::LocalCoroutinePool pool;
    {
        auto warm = pool.Acquire([](cortex::CoroutineSuspendContext&) {});
        warm.Resume();
    } // parks a started coroutine

    bool ran = false;
    {
        auto coroutine = pool.Acquire([&ran](cortex::CoroutineSuspendContext&) {
            ran = true;
        });
    } // released without Resume: must NOT run the body
    EXPECT_FALSE(ran);

    // The parked coroutine is still usable afterwards.
    bool reused_ran = false;
    auto coroutine = pool.Acquire([&reused_ran](cortex::CoroutineSuspendContext&) {
        reused_ran = true;
    });
    coroutine.Resume();
    EXPECT_TRUE(reused_ran);
}

TEST(CoroutinePoolTest, DoubleRebindBeforeResumeIsAllowedOnWarmCoroutine) {
    cortex::LocalCoroutinePool pool;
    {
        auto warm = pool.Acquire([](cortex::CoroutineSuspendContext&) {});
        warm.Resume();
    } // the next Acquire pops a warm (already started) coroutine

    bool first_ran = false;
    bool second_ran = false;
    auto coroutine = pool.Acquire([&first_ran](cortex::CoroutineSuspendContext&) {
        first_ran = true;
    });
    coroutine.Rebind([&second_ran](cortex::CoroutineSuspendContext&) {
        second_ran = true;
    });
    coroutine.Resume();
    EXPECT_FALSE(first_ran);
    EXPECT_TRUE(second_ran);
}

#ifndef CORTEX_EMSCRIPTEN
TEST(CoroutinePoolTest, ThreadSafePoolParallelAcquireRelease) {
    cortex::CoroutinePool pool({.max_parked = 8});
    std::atomic<int> completed {0};

    constexpr int kThreads = 4;
    constexpr int kIterationsPerThread = 250;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&pool, &completed] {
            for (int i = 0; i < kIterationsPerThread; ++i) {
                auto coroutine = pool.Acquire([&completed](cortex::CoroutineSuspendContext& ctx) {
                    ctx.Suspend();
                    completed.fetch_add(1, std::memory_order_relaxed);
                });
                coroutine.Resume();
                coroutine.Resume();
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(completed.load(), kThreads * kIterationsPerThread);
}
#endif
```

- [ ] **Step 2: Register the test in `tests/CMakeLists.txt`**

After the `cortex_fiber_teardown_test` block (line ~41), add:

```cmake
add_executable(cortex_coroutine_pool_test coroutine_pool_test.cpp)
target_link_libraries(cortex_coroutine_pool_test PRIVATE cortex::cortex GTest::gtest_main)
cortex_apply_warnings(cortex_coroutine_pool_test)
cortex_apply_sanitizers(cortex_coroutine_pool_test)
```

In the `if(EMSCRIPTEN)` branch add, alongside the existing entries:

```cmake
    set_target_properties(cortex_coroutine_pool_test PROPERTIES SUFFIX ".js")
    add_test(NAME cortex_wasm_coroutine_pool_test COMMAND node cortex_coroutine_pool_test.js)
    target_link_options(cortex_coroutine_pool_test PRIVATE ${WASM_TEST_LINK_OPTIONS})
```

(the `target_link_options` line must come after `WASM_TEST_LINK_OPTIONS` is set). In the `else()` branch add:

```cmake
    gtest_discover_tests(cortex_coroutine_pool_test)
```

- [ ] **Step 3: Run to verify it fails**

Run: `cmake --build build/native-release -j 2>&1 | tail -5`
Expected: FAIL — `'cortex/coroutine_pool.hpp' file not found`.

- [ ] **Step 4: Create `include/cortex/coroutine_pool.hpp`:**

```cpp
#pragma once

#include <cstddef>
#include <memory>

#include <cortex/coroutine.hpp>
#include <cortex/coroutine_body.hpp>
#include <cortex/detail/null_mutex.hpp>
#include <cortex/memory_resource.hpp>

/**
 * @file coroutine_pool.hpp
 * @brief Pool of reusable stackful coroutines.
 */

namespace cortex {

namespace detail {
class CoroutineImpl;

// Shared pool state (free list + config + locking policy). Defined in
// coroutine_pool.cpp; handles keep it alive via shared_ptr so they may
// safely outlive the pool object.
template <bool ThreadSafe>
struct BasicCoroutinePoolState;
} // namespace detail

/**
 * @struct CoroutinePoolConfig
 * @brief Configuration options for BasicCoroutinePool.
 */
struct CoroutinePoolConfig {
    /// Stack size for every coroutine created by the pool. One pool serves
    /// one stack size; use separate pools for different sizes.
    std::size_t stack_size_bytes = Coroutine::kDefaultStackSizeBytes;
    /// Retention cap: coroutines released beyond this are destroyed.
    std::size_t max_parked = 64;
    /// Resource used for impl and stack allocations on pool misses.
    /// @note The thread-safe pool calls this resource from multiple threads;
    /// PooledMemoryResource is not thread-safe, so keep the (thread-safe)
    /// default there.
    MemoryResourceSharedPtr memory_resource = GetDefaultMemoryResource();
};

template <bool ThreadSafe>
class BasicCoroutinePool;

/**
 * @class BasicPooledCoroutine
 * @brief Move-only handle to a reusable coroutine owned by a pool.
 *
 * Destruction (or Release()) returns the coroutine to its pool. The handle
 * itself is not thread-safe; the pool is (in the ThreadSafe instantiation).
 */
template <bool ThreadSafe>
class BasicPooledCoroutine final {
public:
    BasicPooledCoroutine(BasicPooledCoroutine&& other) noexcept;
    BasicPooledCoroutine& operator=(BasicPooledCoroutine&& other) noexcept;
    BasicPooledCoroutine(const BasicPooledCoroutine&) = delete;
    BasicPooledCoroutine& operator=(const BasicPooledCoroutine&) = delete;

    /// Releases the coroutine back to the pool (see Release()).
    ~BasicPooledCoroutine();

    /**
     * @brief Resumes the coroutine; same semantics as Coroutine::Resume.
     * @throws ResumeOnDoneCoroutineError if the body already finished.
     */
    void Resume();

    /// @brief True once the current body has finished.
    [[nodiscard]] bool IsDone() const noexcept;

    /// @brief The coroutine's stack size in bytes.
    [[nodiscard]] std::size_t GetStackSize() const noexcept;

    /**
     * @brief Re-arms the coroutine with a new body, skipping the free list.
     *
     * The fastest reuse path: a tight loop can Rebind + Resume a held handle
     * without touching the pool. Valid when the current body finished or
     * never started.
     *
     * @throws std::logic_error if a started body has not finished.
     * @throws std::invalid_argument if the body is null.
     */
    void Rebind(CoroutineBody body);

    /**
     * @brief Returns the coroutine to the pool; the handle becomes empty.
     *
     * A started-but-unfinished body is force-unwound first: destructors on
     * the coroutine stack run, and a body with no suspend points executes to
     * completion during the unwind. If the pool is gone or full, the
     * coroutine is destroyed instead. No-op on an empty handle.
     */
    void Release();

private:
    friend class BasicCoroutinePool<ThreadSafe>;

    BasicPooledCoroutine(detail::CoroutineImpl* impl,
                         std::shared_ptr<detail::BasicCoroutinePoolState<ThreadSafe>> state) noexcept;

    detail::CoroutineImpl* impl_ {nullptr};
    std::shared_ptr<detail::BasicCoroutinePoolState<ThreadSafe>> state_;
};

/**
 * @class BasicCoroutinePool
 * @brief Recycles whole coroutines: live context and stack, not just memory.
 *
 * Acquire() pops a parked coroutine and rebinds it to the new body — no
 * stack allocation and no context setup — falling back to creating a fresh
 * reusable coroutine when the free list is empty. Released coroutines park
 * with their context intact, up to CoroutinePoolConfig::max_parked. A
 * released body's captures are dropped at Release() time (the pool re-arms
 * the coroutine with an inert body), so the free list never pins user state.
 *
 * @tparam ThreadSafe true guards the free list with std::mutex; false uses a
 * no-op mutex and must only be used from a single thread. The ThreadSafe
 * instantiation targets native multithreaded use; WASM builds are
 * single-threaded today.
 */
template <bool ThreadSafe>
class BasicCoroutinePool final {
public:
    BasicCoroutinePool();

    /**
     * @throws std::invalid_argument if stack_size_bytes is 0 or
     * memory_resource is null.
     */
    explicit BasicCoroutinePool(CoroutinePoolConfig config);

    BasicCoroutinePool(const BasicCoroutinePool&) = delete;
    BasicCoroutinePool(BasicCoroutinePool&&) = delete;
    BasicCoroutinePool& operator=(const BasicCoroutinePool&) = delete;
    BasicCoroutinePool& operator=(BasicCoroutinePool&&) = delete;

    /// Closes the pool and destroys (force-unwinds) all parked coroutines.
    /// Outstanding handles stay valid and destroy their coroutine on release.
    ~BasicCoroutinePool();

    /**
     * @brief Pops a parked coroutine rebound to body, or creates a fresh one.
     *
     * Never blocks and never runs the body; call Resume() on the handle.
     *
     * @throws std::invalid_argument if the body is null.
     */
    [[nodiscard]] BasicPooledCoroutine<ThreadSafe> Acquire(CoroutineBody body);

    /**
     * @brief Pre-creates parked coroutines until at least count are parked.
     *
     * Reserved coroutines are created unstarted (no context switches); the
     * count is capped by CoroutinePoolConfig::max_parked.
     */
    void Reserve(std::size_t count);

    /// @brief Number of coroutines currently parked in the free list.
    [[nodiscard]] std::size_t GetParkedCount() const;

private:
    std::shared_ptr<detail::BasicCoroutinePoolState<ThreadSafe>> state_;
};

/// Thread-safe pool (std::mutex-guarded free list).
using CoroutinePool = BasicCoroutinePool<true>;
/// Handle type produced by CoroutinePool.
using PooledCoroutine = BasicPooledCoroutine<true>;
/// Single-threaded pool: zero locking, same contract as PooledMemoryResource.
using LocalCoroutinePool = BasicCoroutinePool<false>;
/// Handle type produced by LocalCoroutinePool.
using LocalPooledCoroutine = BasicPooledCoroutine<false>;

} // namespace cortex
```

- [ ] **Step 5: Create `src/coroutine_pool.cpp`:**

```cpp
#include <cortex/config.hpp>
#include <cortex/coroutine_pool.hpp>

#include <cassert>
#include <mutex>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(CORTEX_EMSCRIPTEN)
#include "detail/coroutine_emscripten_impl.hpp"
#else
#include "detail/coroutine_native_impl.hpp"
#endif

namespace cortex {

namespace detail {

// Non-template pool logic. Locking is the wrapper's job. Deliberately NOT in
// an anonymous namespace: it is a member of the externally-declared
// BasicCoroutinePoolState template (GCC -Wsubobject-linkage).
struct PoolCore {
    explicit PoolCore(CoroutinePoolConfig config_arg)
        : config(std::move(config_arg)) {
        parked.reserve(16);
    }

    CoroutineImpl* CreateImpl(CoroutineBody body) const {
        const auto& resource = config.memory_resource;
        void* memory = resource->Allocate(sizeof(CoroutineImpl), alignof(CoroutineImpl));
        try {
            return new (memory) CoroutineImpl(std::move(body), config.stack_size_bytes, resource,
                                              /*reusable=*/true);
        } catch (...) {
            resource->Deallocate(memory, sizeof(CoroutineImpl), alignof(CoroutineImpl));
            throw;
        }
    }

    void DestroyImpl(CoroutineImpl* impl) const noexcept {
        const auto& resource = config.memory_resource;
        impl->~CoroutineImpl();
        resource->Deallocate(impl, sizeof(CoroutineImpl), alignof(CoroutineImpl));
    }

    CoroutinePoolConfig config;
    bool closed {false};
    std::vector<CoroutineImpl*> parked;
};

template <bool ThreadSafe>
struct BasicCoroutinePoolState {
    using Mutex = std::conditional_t<ThreadSafe, std::mutex, NullMutex>;

    explicit BasicCoroutinePoolState(CoroutinePoolConfig config)
        : core(std::move(config)) {}

    ~BasicCoroutinePoolState() {
        // Backstop: normally the pool destructor drains the free list; this
        // covers a state that dies together with the last outstanding handle.
        for (CoroutineImpl* impl : core.parked) {
            core.DestroyImpl(impl);
        }
    }

    PoolCore core;
    mutable Mutex mutex;
};

} // namespace detail

// ---------------------------------------------------------------------------
// BasicCoroutinePool

template <bool ThreadSafe>
BasicCoroutinePool<ThreadSafe>::BasicCoroutinePool()
    : BasicCoroutinePool(CoroutinePoolConfig {}) {}

template <bool ThreadSafe>
BasicCoroutinePool<ThreadSafe>::BasicCoroutinePool(CoroutinePoolConfig config) {
    if (config.stack_size_bytes == 0) {
        throw std::invalid_argument("stack_size_bytes is 0.");
    }
    if (!config.memory_resource) {
        throw std::invalid_argument("memory_resource is null.");
    }
    state_ = std::make_shared<detail::BasicCoroutinePoolState<ThreadSafe>>(std::move(config));
}

template <bool ThreadSafe>
BasicCoroutinePool<ThreadSafe>::~BasicCoroutinePool() {
    std::vector<detail::CoroutineImpl*> to_destroy;
    {
        std::scoped_lock lock(state_->mutex);
        state_->core.closed = true;
        to_destroy.swap(state_->core.parked);
    }
    // Destroy (force-unwind) outside the lock: the unwind of a parked
    // trampoline is trivial, but keep the pattern uniform with Release.
    for (auto* impl : to_destroy) {
        state_->core.DestroyImpl(impl);
    }
}

template <bool ThreadSafe>
BasicPooledCoroutine<ThreadSafe> BasicCoroutinePool<ThreadSafe>::Acquire(CoroutineBody body) {
    if (!static_cast<bool>(body)) {
        throw std::invalid_argument("coroutine body is null.");
    }

    detail::CoroutineImpl* impl = nullptr;
    {
        std::scoped_lock lock(state_->mutex);
        if (!state_->core.parked.empty()) {
            impl = state_->core.parked.back();
            state_->core.parked.pop_back();
        }
    }

    if (impl) {
        // Exclusively owned once popped; rebind outside the lock.
        impl->Rebind(std::move(body));
    } else {
        impl = state_->core.CreateImpl(std::move(body));
    }

    return BasicPooledCoroutine<ThreadSafe>(impl, state_);
}

template <bool ThreadSafe>
void BasicCoroutinePool<ThreadSafe>::Reserve(std::size_t count) {
    if (count > state_->core.config.max_parked) {
        count = state_->core.config.max_parked;
    }
    for (;;) {
        {
            std::scoped_lock lock(state_->mutex);
            if (state_->core.closed || state_->core.parked.size() >= count) {
                return;
            }
        }
        // Created unstarted: Acquire's Rebind swaps the placeholder body out
        // before the trampoline ever runs, so reserving costs no switches.
        auto* impl = state_->core.CreateImpl([](CoroutineSuspendContext&) {});
        bool parked = false;
        {
            std::scoped_lock lock(state_->mutex);
            if (!state_->core.closed && state_->core.parked.size() < count) {
                state_->core.parked.push_back(impl);
                parked = true;
            }
        }
        if (!parked) {
            state_->core.DestroyImpl(impl);
            return;
        }
    }
}

template <bool ThreadSafe>
std::size_t BasicCoroutinePool<ThreadSafe>::GetParkedCount() const {
    std::scoped_lock lock(state_->mutex);
    return state_->core.parked.size();
}

// ---------------------------------------------------------------------------
// BasicPooledCoroutine

template <bool ThreadSafe>
BasicPooledCoroutine<ThreadSafe>::BasicPooledCoroutine(
    detail::CoroutineImpl* impl, std::shared_ptr<detail::BasicCoroutinePoolState<ThreadSafe>> state) noexcept
    : impl_(impl)
    , state_(std::move(state)) {}

template <bool ThreadSafe>
BasicPooledCoroutine<ThreadSafe>::BasicPooledCoroutine(BasicPooledCoroutine&& other) noexcept
    : impl_(std::exchange(other.impl_, nullptr))
    , state_(std::move(other.state_)) {}

template <bool ThreadSafe>
BasicPooledCoroutine<ThreadSafe>& BasicPooledCoroutine<ThreadSafe>::operator=(
    BasicPooledCoroutine&& other) noexcept {
    if (this != &other) {
        Release();
        impl_ = std::exchange(other.impl_, nullptr);
        state_ = std::move(other.state_);
    }
    return *this;
}

template <bool ThreadSafe>
BasicPooledCoroutine<ThreadSafe>::~BasicPooledCoroutine() {
    Release();
}

template <bool ThreadSafe>
void BasicPooledCoroutine<ThreadSafe>::Resume() {
    assert(impl_);
    impl_->Resume();
}

template <bool ThreadSafe>
bool BasicPooledCoroutine<ThreadSafe>::IsDone() const noexcept {
    assert(impl_);
    return impl_->IsDone();
}

template <bool ThreadSafe>
std::size_t BasicPooledCoroutine<ThreadSafe>::GetStackSize() const noexcept {
    assert(impl_);
    return impl_->GetStackSize();
}

template <bool ThreadSafe>
void BasicPooledCoroutine<ThreadSafe>::Rebind(CoroutineBody body) {
    assert(impl_);
    if (!static_cast<bool>(body)) {
        throw std::invalid_argument("coroutine body is null.");
    }
    impl_->Rebind(std::move(body));
}

template <bool ThreadSafe>
void BasicPooledCoroutine<ThreadSafe>::Release() {
    if (!impl_) {
        return;
    }
    auto* impl = std::exchange(impl_, nullptr);

    // Unwind a started-but-unfinished body before parking. Runs outside the
    // pool lock: destructors on the coroutine stack may re-enter the pool.
    impl->AbortBody();

    // Re-arm with an inert body so the released body's captures are dropped
    // now instead of staying pinned in the free list until the next Acquire.
    impl->Rebind([](CoroutineSuspendContext&) {});

    bool park = false;
    {
        std::scoped_lock lock(state_->mutex);
        if (!state_->core.closed && state_->core.parked.size() < state_->core.config.max_parked) {
            state_->core.parked.push_back(impl);
            park = true;
        }
    }
    if (!park) {
        state_->core.DestroyImpl(impl);
    }
    state_.reset();
}

// ---------------------------------------------------------------------------
// Explicit instantiations: the only two variants that exist.

template class BasicCoroutinePool<true>;
template class BasicCoroutinePool<false>;
template class BasicPooledCoroutine<true>;
template class BasicPooledCoroutine<false>;

} // namespace cortex
```

- [ ] **Step 6: Add `coroutine_pool.cpp` to `src/CMakeLists.txt`**

In the `target_sources(cortex PRIVATE ...)` block, after `pooled_memory_resource.cpp`, add:

```cmake
        coroutine_pool.cpp
```

- [ ] **Step 7: Run native tests to verify they pass**

Run: `cmake --build build/native-release -j && ctest --test-dir build/native-release --output-on-failure -R CoroutinePool`
Expected: all `CoroutinePoolTest.*` PASS.

Then the full suite: `ctest --test-dir build/native-release --output-on-failure`
Expected: 100% pass.

- [ ] **Step 8: Run WASM tests**

Run: `docker compose run --rm test-wasm`
Expected: 100% pass, including `cortex_wasm_coroutine_pool_test`.

- [ ] **Step 9: Commit**

```bash
AISUITE_ALLOW_GIT=1 git add include/cortex/coroutine_pool.hpp src/coroutine_pool.cpp src/CMakeLists.txt tests/coroutine_pool_test.cpp tests/CMakeLists.txt
AISUITE_ALLOW_GIT=1 git commit -m "feat: CoroutinePool / LocalCoroutinePool with PooledCoroutine handles

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 4: BaseCoroutine reuse hooks

**Files:**
- Test: `tests/base_coroutine_test.cpp` (append)
- Modify: `include/cortex/coroutine.hpp`, `src/coroutine.cpp`
- Modify: `include/cortex/base_coroutine.hpp`, `src/base_coroutine.cpp`

- [ ] **Step 1: Write the failing test — append to `tests/base_coroutine_test.cpp`:**

```cpp
namespace {

class ReusableCounter : public cortex::BaseCoroutine {
public:
    ReusableCounter()
        : BaseCoroutine(cortex::Coroutine::kDefaultStackSizeBytes,
                        cortex::GetDefaultMemoryResource(),
                        /*reusable=*/true) {}

    int runs = 0;

    void Reset() {
        ResetCoroutineForReuse();
    }

private:
    void Continuation(cortex::CoroutineSuspendContext&) override {
        ++runs;
    }
};

} // namespace

TEST(BaseCoroutineReuseTest, ContinuationRunsAgainAfterReset) {
    ReusableCounter coroutine;
    EXPECT_EQ(coroutine.GetStackSize(), cortex::Coroutine::kDefaultStackSizeBytes);
    coroutine.Resume();
    EXPECT_TRUE(coroutine.IsDone());
    EXPECT_EQ(coroutine.runs, 1);

    coroutine.Reset();
    EXPECT_FALSE(coroutine.IsDone());
    coroutine.Resume();
    EXPECT_TRUE(coroutine.IsDone());
    EXPECT_EQ(coroutine.runs, 2);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build/native-release -j 2>&1 | tail -5`
Expected: FAIL — no 3-argument `BaseCoroutine` constructor, no `ResetCoroutineForReuse`, no `GetStackSize`.

- [ ] **Step 3: Add internal hooks to `include/cortex/coroutine.hpp`**

In the `private:` section of `class Coroutine` (before `struct ImplDeleter`), add:

```cpp
    friend class BaseCoroutine;

    // Internal support for BaseCoroutine reuse (e.g. tiny_fiber fiber
    // recycling): a reusable coroutine parks after its body finishes instead
    // of letting the context die, and RebindForReuseInternal re-arms it.
    static Coroutine MakeInternal(CoroutineBody body,
                                  std::size_t stack_size_bytes,
                                  MemoryResourceSharedPtr resource,
                                  bool reusable);
    void RebindForReuseInternal();
```

- [ ] **Step 4: Implement in `src/coroutine.cpp`**

Replace the body of `Coroutine::Make` with a delegation, and add the two new members:

```cpp
Coroutine Coroutine::Make(CoroutineBody body, std::size_t stack_size_bytes, MemoryResourceSharedPtr resource) {
    return MakeInternal(std::move(body), stack_size_bytes, std::move(resource), /*reusable=*/false);
}

Coroutine Coroutine::MakeInternal(CoroutineBody body,
                                  std::size_t stack_size_bytes,
                                  MemoryResourceSharedPtr resource,
                                  bool reusable) {
    if (!static_cast<bool>(body)) {
        throw std::invalid_argument("coroutine body is null.");
    }

    if (stack_size_bytes == 0) {
        throw std::invalid_argument("stack_size_bytes is 0.");
    }

    if (!resource) {
        throw std::invalid_argument("memory_resource is null.");
    }

    void* ptr = resource->Allocate(sizeof(detail::CoroutineImpl), alignof(detail::CoroutineImpl));
    try {
        auto* impl = new (ptr) detail::CoroutineImpl(std::move(body), stack_size_bytes, resource, reusable);
        return Coroutine(std::unique_ptr<detail::CoroutineImpl, ImplDeleter>(impl, ImplDeleter {std::move(resource)}));
    } catch (...) {
        resource->Deallocate(ptr, sizeof(detail::CoroutineImpl), alignof(detail::CoroutineImpl));
        throw;
    }
}

void Coroutine::RebindForReuseInternal() {
    assert(impl_);
    impl_->Rebind();
}
```

(The old validation-and-construction body moves into `MakeInternal`; `Make` keeps its exact public behavior.)

- [ ] **Step 5: Extend `include/cortex/base_coroutine.hpp`**

Add a public `GetStackSize()` next to `IsDone()`:

```cpp
    /**
     * @brief Gets the allocated stack size of the underlying coroutine.
     * @return The stack size in bytes.
     */
    [[nodiscard]] std::size_t GetStackSize() const noexcept {
        return coroutine_.GetStackSize();
    }
```

Change the protected constructor declaration and add the reuse hook:

```cpp
    /**
     * @brief Constructs a new BaseCoroutine.
     *
     * @param stack_size_bytes The size of the stack to allocate for the coroutine (default: 256KB).
     * @param resource The memory resource to use for stack and implementation allocation (default:
     * GetDefaultMemoryResource()).
     * @param reusable When true, the coroutine parks after Continuation()
     * finishes instead of destroying its context; ResetCoroutineForReuse()
     * re-arms it for another run on the same stack.
     */
    explicit BaseCoroutine(std::size_t stack_size_bytes = Coroutine::kDefaultStackSizeBytes,
                           MemoryResourceSharedPtr resource = GetDefaultMemoryResource(),
                           bool reusable = false);

    /**
     * @brief Re-arms a finished reusable coroutine.
     *
     * The next Resume() runs Continuation() again on the same stack and
     * context. Only valid for reusable coroutines whose Continuation()
     * finished (or never started).
     */
    void ResetCoroutineForReuse();
```

- [ ] **Step 6: Implement in `src/base_coroutine.cpp`**

```cpp
BaseCoroutine::BaseCoroutine(const std::size_t stack_size_bytes, MemoryResourceSharedPtr resource, bool reusable)
    : coroutine_(Coroutine::MakeInternal(
          [this](CoroutineSuspendContext& self) {
              this->Continuation(self);
          },
          stack_size_bytes,
          std::move(resource),
          reusable)) {}

void BaseCoroutine::ResetCoroutineForReuse() {
    coroutine_.RebindForReuseInternal();
}
```

- [ ] **Step 7: Run tests to verify they pass**

Run: `cmake --build build/native-release -j && ctest --test-dir build/native-release --output-on-failure`
Expected: 100% pass including `BaseCoroutineReuseTest.ContinuationRunsAgainAfterReset`.

- [ ] **Step 8: Commit**

```bash
AISUITE_ALLOW_GIT=1 git add include/cortex/coroutine.hpp src/coroutine.cpp include/cortex/base_coroutine.hpp src/base_coroutine.cpp tests/base_coroutine_test.cpp
AISUITE_ALLOW_GIT=1 git commit -m "feat: reusable-mode hooks on Coroutine and BaseCoroutine

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 5: Scheduler fiber reuse

**Files:**
- Test: `tests/tiny_fiber_test.cpp` (append)
- Modify: `include/cortex/tiny_fiber/detail/fiber.hpp`, `src/tiny_fiber/fiber.cpp`
- Modify: `include/cortex/tiny_fiber/scheduler.hpp`, `src/tiny_fiber/scheduler.cpp`

- [ ] **Step 1: Write the failing tests — append to `tests/tiny_fiber_test.cpp`:**

```cpp
// ============================================================================
// Fiber Reuse Tests
// ============================================================================

TEST(TinyFiberReuseTest, FinishedFibersAreReusedNotRecycledThroughMemoryPool) {
    auto pooled = std::make_shared<cortex::PooledMemoryResource>();
    tf::Scheduler::Run(
        [&pooled] {
            for (int i = 0; i < 5; ++i) {
                auto future = tf::Spawn([] {
                    return 1;
                });
                (void)future.Get();
            }
            // Finished fibers were parked for reuse, so their 256KB stacks
            // never went back to the memory pool's free lists. (Small blocks
            // like future state do round-trip the pool.)
            EXPECT_LT(pooled->GetCachedBytes(), cortex::Coroutine::kDefaultStackSizeBytes);
        },
        tf::Scheduler::Config {.memory_resource = pooled});
}

TEST(TinyFiberReuseTest, ZeroMaxPooledFibersDisablesReuse) {
    auto pooled = std::make_shared<cortex::PooledMemoryResource>();
    tf::Scheduler::Run(
        [&pooled] {
            for (int i = 0; i < 5; ++i) {
                auto future = tf::Spawn([] {
                    return 1;
                });
                (void)future.Get();
            }
            // Without fiber reuse, destroyed fibers push their stacks into
            // the memory pool's free lists.
            EXPECT_GE(pooled->GetCachedBytes(), cortex::Coroutine::kDefaultStackSizeBytes);
        },
        tf::Scheduler::Config {.memory_resource = pooled, .max_pooled_fibers = 0});
}

TEST(TinyFiberReuseTest, ReusedFibersDeliverResultsAndWakeWaiters) {
    tf::Scheduler::Run([] {
        for (int i = 0; i < 100; ++i) {
            auto future = tf::Spawn([i] {
                tf::Yield();
                return i * 2;
            });
            EXPECT_EQ(future.Get(), i * 2);
        }
    });
}

TEST(TinyFiberReuseTest, ReuseAcrossWavesOfConcurrentFibers) {
    tf::Scheduler::Run([] {
        for (int wave = 0; wave < 5; ++wave) {
            std::vector<tf::Future<int>> futures;
            futures.reserve(32);
            for (int i = 0; i < 32; ++i) {
                futures.push_back(tf::Spawn([i] {
                    tf::Yield();
                    return i;
                }));
            }
            for (int i = 0; i < 32; ++i) {
                EXPECT_EQ(futures[static_cast<std::size_t>(i)].Get(), i);
            }
        }
    });
}

TEST(TinyFiberReuseTest, TeardownWithParkedFibersIsClean) {
    auto scheduler = tf::Scheduler::Create([] {
        auto future = tf::Spawn([] {
            return 7;
        });
        (void)future.Get();
        tf::Yield();
    });
    while (scheduler->Step()) {
    }
    scheduler.reset(); // destroys parked (reusable) fibers cleanly
}
```

Also add these includes at the top of the file if not present:

```cpp
#include <cortex/pooled_memory_resource.hpp>
#include <memory>
```

- [ ] **Step 2: Run to verify failures**

Run: `cmake --build build/native-release -j 2>&1 | tail -5`
Expected: FAIL — `Config` has no member `max_pooled_fibers`. (After adding only the config field, `FinishedFibersAreReusedNotRecycledThroughMemoryPool` must FAIL at the `EXPECT_LT` until reuse is implemented — verify both stages if convenient.)

- [ ] **Step 3: Extend `include/cortex/tiny_fiber/detail/fiber.hpp`**

Change the constructor declaration and add `ResetForReuse` (after the constructor):

```cpp
    // Construction goes through Scheduler::SpawnFiberInternal so the scheduler can
    // assign unique IDs. The constructor takes the ID directly. When reusable is
    // true the underlying coroutine parks after the body finishes so the whole
    // fiber can be re-armed with ResetForReuse.
    Fiber(Id id, Body body, std::size_t stack_size, MemoryResourceSharedPtr resource, bool reusable);

    // Re-arm a finished fiber with a new identity and body: resets state and
    // waiters and rebinds the parked coroutine. Only valid on fibers
    // constructed with reusable == true whose state is Finished.
    void ResetForReuse(Id id, Body body);
```

- [ ] **Step 4: Implement in `src/tiny_fiber/fiber.cpp`**

```cpp
Fiber::Fiber(Id id, Body body, std::size_t stack_size, MemoryResourceSharedPtr resource, bool reusable)
    : BaseCoroutine(stack_size, std::move(resource), reusable)
    , id_(id)
    , body_(std::move(body)) {}

void Fiber::ResetForReuse(Id id, Body body) {
    assert(state_ == FiberState::Finished);
    id_ = id;
    body_ = std::move(body);
    state_ = FiberState::Ready;
    suspend_ctx_ = nullptr;
    inline_waiter_count_ = 0;
    overflow_waiters_.clear();
    ResetCoroutineForReuse();
}
```

- [ ] **Step 5: Extend `include/cortex/tiny_fiber/scheduler.hpp`**

Add to `Config` after `memory_resource`:

```cpp
        // Finished fibers are parked (up to this many) and reused by the next
        // Spawn instead of being destroyed: no object construction and no
        // context setup on the warm path. 0 disables fiber reuse.
        std::size_t max_pooled_fibers = 64;
```

Add a member next to `vacant_slots_`:

```cpp
    // Finished fibers parked for reuse. Not in fiber_slots_: a parked fiber
    // has no identity until ResetForReuse assigns a fresh id and slot.
    std::vector<detail::FiberPtr> free_fibers_;
```

- [ ] **Step 6: Implement reuse in `src/tiny_fiber/scheduler.cpp`**

Replace `ProcessPendingCleanup` with:

```cpp
void Scheduler::ProcessPendingCleanup() {
    for (auto id : pending_cleanup_) {
        const auto index = static_cast<std::size_t>(id & kSlotIndexMask);
        if (index < fiber_slots_.size() && fiber_slots_[index].id == id) {
            auto& fiber = fiber_slots_[index].fiber;
            const bool park = !stopping_ && free_fibers_.size() < config_.max_pooled_fibers &&
                              fiber->GetStackSize() == config_.default_stack_size;
            if (park) {
                free_fibers_.push_back(std::move(fiber));
            }
            fiber.reset();
            fiber_slots_[index].id = 0;
            vacant_slots_.push_back(static_cast<std::uint32_t>(index));
        }
    }
    pending_cleanup_.clear();
}
```

Replace `SpawnFiberInternal` with:

```cpp
detail::Fiber::Id Scheduler::SpawnFiberInternal(detail::Fiber::Body func, std::size_t stack_size) {
    std::uint32_t index = 0;
    if (!vacant_slots_.empty()) {
        index = vacant_slots_.back();
        vacant_slots_.pop_back();
    } else {
        if (fiber_slots_.size() > kSlotIndexMask) {
            throw std::length_error("Too many live fibers in one scheduler");
        }
        index = static_cast<std::uint32_t>(fiber_slots_.size());
        fiber_slots_.emplace_back();
    }

    const auto id = (next_sequence_++ << kSlotIndexBits) | index;

    // Warm path: re-arm a parked fiber. Reuses the fiber object, its stack
    // and its live coroutine context — no allocation, no context setup.
    if (!free_fibers_.empty() && stack_size == config_.default_stack_size) {
        auto fiber = std::move(free_fibers_.back());
        free_fibers_.pop_back();

        try {
            fiber->ResetForReuse(id, std::move(func));
        } catch (...) {
            vacant_slots_.push_back(index);
            throw;
        }

        detail::Fiber* fiber_raw_ptr = fiber.get();
        fiber_slots_[index].id = id;
        fiber_slots_[index].fiber = std::move(fiber);
        ready_queue_.push_back(fiber_raw_ptr);
        return id;
    }

    // Place the fiber object itself in MemoryResource storage so that with
    // the default pooled resource a spawn performs no system allocations.
    const auto& resource = config_.memory_resource;
    void* memory = resource->Allocate(sizeof(detail::Fiber), alignof(detail::Fiber));

    detail::Fiber* fiber_raw_ptr = nullptr;
    try {
        fiber_raw_ptr = new (memory) detail::Fiber(id, std::move(func), stack_size, resource,
                                                   /*reusable=*/config_.max_pooled_fibers > 0);
    } catch (...) {
        resource->Deallocate(memory, sizeof(detail::Fiber), alignof(detail::Fiber));
        vacant_slots_.push_back(index);
        throw;
    }

    fiber_slots_[index].id = id;
    fiber_slots_[index].fiber = detail::FiberPtr(fiber_raw_ptr, detail::FiberDeleter {resource.get()});
    ready_queue_.push_back(fiber_raw_ptr);

    return id;
}
```

In the destructor, drain the free list right before `fiber_slots_.clear();`:

```cpp
    running_ = false;
    // Destroying parked fibers unwinds their parked trampolines; clearing the
    // slots then triggers forced unwinding for any fiber that didn't exit.
    free_fibers_.clear();
    fiber_slots_.clear();
    g_current_scheduler = nullptr;
```

- [ ] **Step 7: Run tests to verify they pass**

Run: `cmake --build build/native-release -j && ctest --test-dir build/native-release --output-on-failure`
Expected: 100% pass including all five `TinyFiberReuseTest.*`.

- [ ] **Step 8: Run WASM tests**

Run: `docker compose run --rm test-wasm`
Expected: 100% pass.

- [ ] **Step 9: Commit**

```bash
AISUITE_ALLOW_GIT=1 git add include/cortex/tiny_fiber/detail/fiber.hpp src/tiny_fiber/fiber.cpp include/cortex/tiny_fiber/scheduler.hpp src/tiny_fiber/scheduler.cpp tests/tiny_fiber_test.cpp
AISUITE_ALLOW_GIT=1 git commit -m "perf(tiny_fiber): reuse finished fibers via coroutine rebind

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 6: Benchmarks and docs

**Files:**
- Modify: `benchmarks/bench_main.cpp`
- Modify: `DEVELOPMENT.md`

- [ ] **Step 1: Add pool benchmarks to `benchmarks/bench_main.cpp`**

Add the include:

```cpp
#include <cortex/coroutine_pool.hpp>
```

Add after `BenchCoroutineCreateDestroy`:

```cpp
// Acquire + run + auto-release against a warm pool: the steady-state cost of
// running a task on a pooled coroutine.
void BenchPoolAcquireRelease(const char* name, auto& pool) {
    RunBench(name, [&pool] {
        {
            auto warm = pool.Acquire([](cortex::CoroutineSuspendContext&) {
            });
            warm.Resume();
        }

        constexpr std::uint64_t kIterations = 200'000;
        return TimeNsPerOp(kIterations, [&pool] {
            auto coroutine = pool.Acquire([](cortex::CoroutineSuspendContext&) {
            });
            coroutine.Resume();
        });
    });
}

// Rebind + resume on a held handle: the floor for coroutine reuse.
void BenchPoolRebindResume() {
    RunBench("pool_rebind_resume (handle reuse)", [] {
        cortex::LocalCoroutinePool pool;
        auto coroutine = pool.Acquire([](cortex::CoroutineSuspendContext&) {
        });
        coroutine.Resume();

        constexpr std::uint64_t kIterations = 500'000;
        return TimeNsPerOp(kIterations, [&] {
            coroutine.Rebind([](cortex::CoroutineSuspendContext&) {
            });
            coroutine.Resume();
        });
    });
}
```

Register in `main()` after the `BenchCoroutineCreateDestroy` calls:

```cpp
    {
        cortex::LocalCoroutinePool local_pool;
        BenchPoolAcquireRelease("pool_acquire_release (local pool)", local_pool);
    }
    {
        cortex::CoroutinePool shared_pool;
        BenchPoolAcquireRelease("pool_acquire_release (thread-safe pool)", shared_pool);
    }
    BenchPoolRebindResume();
```

- [ ] **Step 2: Build and run the benchmarks**

```bash
cmake -B build/native-release -DCMAKE_BUILD_TYPE=Release -DCORTEX_BUILD_TESTS=ON -DCORTEX_BUILD_BENCHMARKS=ON
cmake --build build/native-release --target cortex_bench -j
./build/native-release/benchmarks/cortex_bench
```

Expected: the two `pool_acquire_release` benches land well under `coroutine_create_destroy (pooled alloc)`, `pool_rebind_resume` under those, and `fiber_spawn_join (default config)` drops meaningfully below its previous ~253 ns/op. Record the numbers for the final summary. (`benchmarks/compare.py` prints `new` for benchmarks absent from base — no CI change needed.)

- [ ] **Step 3: Update `DEVELOPMENT.md`**

In the **Performance** section, after the existing bullet about fiber stack recycling, add:

```markdown
- Whole coroutines are recyclable through `cortex::CoroutinePool` (thread-safe, `std::mutex`) and `cortex::LocalCoroutinePool` (single-threaded, zero locking): `Acquire` re-arms a parked coroutine — stack and live context included — instead of creating a new one, and `PooledCoroutine::Rebind` reuses a held handle without touching the pool at all. `tiny_fiber::Scheduler` recycles finished fibers the same way (`Scheduler::Config::max_pooled_fibers`, default 64; set 0 to disable).
```

- [ ] **Step 4: Commit**

```bash
AISUITE_ALLOW_GIT=1 git add benchmarks/bench_main.cpp DEVELOPMENT.md
AISUITE_ALLOW_GIT=1 git commit -m "bench: coroutine pool acquire/release and rebind benchmarks

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 7: Full verification and formatting

- [ ] **Step 1: Format**

Run: `./format` then `AISUITE_ALLOW_GIT=1 git diff --stat`
Expected: only files touched by this work (if `./format` reflows something, review and keep).

- [ ] **Step 2: Full native + WASM suites via Docker**

```bash
docker compose run --rm test-native
docker compose run --rm test-wasm
```

Expected: both exit 0, all tests pass.

- [ ] **Step 3: Sanitizer run (Linux ASan/UBSan — do NOT run ASan on the host Mac)**

Run: `CORTEX_USE_SANITIZERS=ON docker compose run --rm test-native`
Expected: exit 0, no ASan reports. Watch specifically for new-delete-type-mismatch (impl (de)allocation must use identical `(bytes, alignment)` pairs — it does: `sizeof/alignof(CoroutineImpl)` on both sides) and use-after-return on recycled contexts.

- [ ] **Step 4: Benchmark A/B sanity check**

```bash
./build/native-release/benchmarks/cortex_bench --csv > /tmp/head.csv
AISUITE_ALLOW_GIT=1 git stash list   # confirm clean tree first
```

Compare `fiber_spawn_join (default config)` against the pre-change value (~253 ns/op on this machine; re-measure on the base commit with `git worktree` if an exact A/B is wanted — CI runs the authoritative comparison via `benchmarks/ci_bench_check.sh`).
Expected: no benchmark regressed; `fiber_spawn_join` improved.

- [ ] **Step 5: Commit any formatting deltas**

```bash
AISUITE_ALLOW_GIT=1 git add -A
AISUITE_ALLOW_GIT=1 git commit -m "style: apply formatter

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

(Skip if the tree is clean.)

---

## Spec coverage checklist

- Trampoline + `Rebind` + destructor change, both backends → Tasks 1, 2
- `BasicCoroutinePool<ThreadSafe>` / aliases / `NullMutex` / explicit instantiation → Task 3
- `PooledCoroutine` handle: `Resume`/`IsDone`/`GetStackSize`/`Rebind`/`Release`, auto-release, unwind-on-early-release, shared-state lifetime safety → Task 3
- `Reserve` (unstarted, no switches), `max_parked` eviction, `GetParkedCount` → Task 3
- Scheduler fiber reuse, `max_pooled_fibers`, free-list teardown → Tasks 4, 5
- Error-handling table → covered by tests in Tasks 3–5
- Benchmarks + CI compatibility + docs → Task 6
- ASan validation → Task 7
