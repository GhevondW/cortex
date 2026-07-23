# Coroutine Pool — Design

**Date:** 2026-07-21
**Status:** Approved

## Goal

Eliminate the remaining per-spawn cost of stackful coroutines by pooling whole
coroutine *objects* (live context + stack), not just stack memory. PR #39's
`PooledMemoryResource` already recycles stack blocks (native spawn+join went
654 → 253 ns/op); what is still paid on every spawn is object construction,
Boost.Context / Asyncify context-record setup, and teardown. Under Emscripten
Asyncify the context setup is far more expensive than native, so pooling wins
more there.

Scope, as agreed:

1. A **public, standalone** pool API: `CoroutinePool` handing out a new
   `PooledCoroutine` handle type.
2. **Thread-safety selected at compile time**: `BasicCoroutinePool<bool
   ThreadSafe>`. The `true` variant uses `std::mutex`; the `false` variant uses
   a no-op mutex and compiles to zero locking for single-threaded contexts
   (WASM, `tiny_fiber`).
3. The library **uses the pooling mechanism internally wherever it pays off** —
   concretely, `tiny_fiber::Scheduler` reuses finished `Fiber` objects via the
   same rebind primitive (mutex-free).

## 1. Core primitive — rebindable trampoline coroutines

`detail::CoroutineImpl` (both `coroutine_native_impl` and
`coroutine_emscripten_impl`) gains a **reusable mode**. Instead of running the
body once and letting the context die, the entry function loops:

```
for (;;) {
    run body_                    // exceptions captured, as today
    is_done_ = true
    park (suspend back to resumer)
    if (is_unwinding_) break     // impl/pool destruction
    // else: Rebind() happened — loop and run the new body
}
```

- `Rebind(CoroutineBody)` — precondition `IsDone()`: moves the new body into
  the member slot, resets `is_done_`. The next `Resume()` wakes the parked
  trampoline, which runs the new body on the same stack and context.
- A body-less `Rebind()` overload serves `BaseCoroutine` subclasses whose body
  is always `[this](ctx) { Continuation(ctx); }` — reuse there is only a flag
  reset, no fu2 move.
- **Switch count per run is unchanged** (one switch in, one out); the park
  replaces the final return. The native impl moves the body from the lambda
  capture into a member (the Emscripten impl already stores it as a member).
- **Destructor change (both backends):** unwind whenever the context is still
  alive — parked *or* mid-body — not just when `!is_done_`. A parked
  trampoline wakes with `is_unwinding_` set and exits the loop cleanly
  (native: returns the sink; Emscripten: one final swap back).
- `Resume()` on a done-but-parked coroutine still throws
  `ResumeOnDoneCoroutineError`; public `Coroutine` semantics are unchanged.
- Non-reusable coroutines (plain `Coroutine::Make`) keep the current one-shot
  entry function; the trampoline is only built in reusable mode.

## 2. Public API — `include/cortex/coroutine_pool.hpp`

```cpp
struct CoroutinePoolConfig {
    std::size_t stack_size_bytes = Coroutine::kDefaultStackSizeBytes;
    std::size_t max_parked = 64;          // retention cap; extras are destroyed
    MemoryResourceSharedPtr memory_resource = GetDefaultMemoryResource();
};

template <bool ThreadSafe>
class BasicCoroutinePool final {
public:
    explicit BasicCoroutinePool(CoroutinePoolConfig config = {});
    ~BasicCoroutinePool();                        // unwinds all parked coroutines
    PooledCoroutine Acquire(CoroutineBody body);  // pop + rebind; creates on miss
    void Reserve(std::size_t count);              // pre-warm parked coroutines
    [[nodiscard]] std::size_t GetParkedCount() const noexcept;
};

using CoroutinePool      = BasicCoroutinePool<true>;   // std::mutex
using LocalCoroutinePool = BasicCoroutinePool<false>;  // NullMutex — zero cost
```

- **One stack size per pool** (folly FiberManager model). Different stack sizes
  mean different pool instances. The free list stays a flat vector — no
  per-size buckets.
- The template layer contains **only the locking policy**
  (`std::conditional_t<ThreadSafe, std::mutex, detail::NullMutex>`). All logic
  lives in a non-template `detail::CoroutinePoolState`; both instantiations are
  explicit in `src/coroutine_pool.cpp` — no header-only implementation bleed.
- `Acquire` on an empty free list creates a fresh reusable coroutine (never
  blocks). `Reserve(n)` pre-creates parked coroutines so a first frame never
  pays a cold spawn.

### `PooledCoroutine` handle

Move-only. API: `Resume()`, `IsDone()`, `GetStackSize()`,
`Rebind(CoroutineBody)`, `Release()`; destruction auto-releases.

- `Rebind` (throws `std::logic_error` if not done) is the fastest reuse path:
  a tight loop can reuse the handle without ever touching the free list.
- `Release()` of a coroutine whose body has **not** finished force-unwinds it
  first via the existing `ForcedUnwind` path; the trampoline catches it, parks,
  and remains recyclable.
- **Lifetime safety:** the free list lives in a `shared_ptr`-owned
  `detail::CoroutinePoolState` that every handle references. Destroying the
  pool marks the state closed and unwinds parked coroutines; outstanding
  handles observe the closed state on release and destroy their coroutine
  instead of parking. No dangling-pool UB. In the `ThreadSafe` variant the
  mutex lives inside the shared state.

## 3. Scheduler integration — fiber-level reuse

`tiny_fiber::Scheduler` is single-threaded, so it uses the mutex-free rebind
primitive directly on its own `Fiber` objects, reusing the whole fiber (waiter
arrays, fu2 body storage), not just the context:

- `ProcessPendingCleanup()` parks finished fibers into a per-scheduler free
  list instead of destroying them. Bounded by new config
  `Scheduler::Config::max_pooled_fibers = 64`; `0` disables reuse.
- `SpawnFiberInternal()` pops a parked fiber when available: assign a fresh id,
  move the new body into `Fiber::body_`, reset state and waiters, body-less
  `Rebind()` on the base coroutine, push to the ready queue. The cold path is
  unchanged.
- `BaseCoroutine` gains the protected hooks needed: a reusable-mode constructor
  flag and a `ResetForReuse()` that forwards to the impl's body-less rebind.
- A parked fiber is removed from its slot (`ProcessPendingCleanup` retires the
  id and vacates the slot as today); on reuse it is assigned a fresh id and a
  fresh slot index. The scheduler destructor clears the free list, which
  force-unwinds the parked trampolines the same way slot teardown does.
- Expected effect: spawn becomes roughly "pop + move body + queue push" — no
  placement-new, no context-record setup, no fu2 destruction.

## 4. Error handling

| Situation | Behavior |
|---|---|
| Body throws | Captured; rethrown from the `Resume()` that observed completion (as today); coroutine parks and stays recyclable |
| `Acquire` allocation failure | Exception propagates; pool state unchanged |
| `Release`/destroy of unfinished coroutine | Force-unwind, then park (or destroy if over `max_parked`) |
| `Rebind` when not done | `std::logic_error` |
| `Resume` on done coroutine | `ResumeOnDoneCoroutineError` (unchanged) |
| Handle outlives pool | Handle self-destroys its coroutine on release (closed shared state) |
| Scheduler `Stop()`/dtor | Free-list fibers destroyed alongside slot teardown; destruction force-unwinds their parked trampolines |

## 5. Testing & benchmarks

**Unit tests** (native + WASM, `./dev.sh test-all`):

- Pool: acquire/release/rebind lifecycle, exception-then-reuse,
  unwind-on-early-release, `max_parked` eviction, `Reserve`,
  handle-outlives-pool, `GetParkedCount`.
- Thread-safety: native-only multithreaded hammer test for
  `BasicCoroutinePool<true>` (N threads acquiring/resuming/releasing).
- Scheduler: fiber reuse issues fresh IDs, waiter state is clean after reuse,
  `max_pooled_fibers = 0` disables pooling, teardown with parked fibers is
  leak-free.
- Existing suites must stay green.

**Benchmarks** (`benchmarks/bench_main.cpp`):

- New: `pool_acquire_release`, `pool_rebind_resume`.
- Expected: `fiber_spawn_join` drops well below 253 ns/op.
- Existing CI A/B bench check guards regressions; Linux ASan CI validates the
  recycled-context paths. Parked stacks stay live allocations, so no ASan
  unpoison handling is needed (unlike the PR #39 freed-stack recycling).

## Out of scope

- Lock-free free list (revisit only if a benchmark shows mutex contention).
- Per-size buckets inside one pool.
- Pooling for `Generator` and `cortex::async` (the latter is a stub); both can
  adopt the same rebind primitive later.
