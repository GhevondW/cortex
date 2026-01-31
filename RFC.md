# RFC: tiny_fiber - Cooperative Multitasking Module

## Summary

Add a cooperative multitasking submodule (`tiny_fiber`) built on top of `cortex::Coroutine` that provides:
- Fiber-based task scheduling
- `Future<T>` for async result handling
- Cooperative synchronization primitives (Mutex, ConditionVariable, Channel)
- Single-threaded, non-blocking execution model perfect for WASM

## Motivation

WebAssembly environments typically lack:
- `std::thread` support
- Real mutex/condition variable implementations
- Any form of preemptive multitasking

However, many applications need concurrent-style programming for:
- Managing multiple simultaneous operations
- Implementing state machines
- Building responsive UIs with background work
- Game loops with multiple entities

By building on `cortex::Coroutine`, we can provide a familiar async/await-style API that works identically on native and WASM platforms.

## Design Goals

1. **No threading**: Everything runs on a single thread
2. **Explicit yielding**: Fibers must explicitly yield control
3. **WASM-first**: Must work in Emscripten environment
4. **Zero external dependencies**: Only depends on cortex::Coroutine
5. **Ergonomic API**: Familiar patterns for C++ developers

---

## Proposed API

### Namespace

```cpp
namespace cortex::tiny_fiber {
    // All cooperative multitasking primitives
}
```

### Core Types

#### 1. Scheduler

The scheduler manages a queue of ready fibers and runs them cooperatively.

```cpp
namespace cortex::fiber {

class Scheduler {
public:
    // Run the scheduler with an initial fiber
    // Blocks until all fibers complete
    template <typename F>
    static void Run(F&& entry);

    template <typename F>
    static void Run(F&& entry, Config config);

    // Create a scheduler for manual stepping (WASM integration)
    // Use Step() to advance execution one fiber at a time
    template <typename F>
    static Scheduler Create(F&& entry);

    template <typename F>
    static Scheduler Create(F&& entry, Config config);

    // Run one step - picks one ready fiber and runs until it yields
    // Returns true if there's more work, false if all fibers done
    bool Step();

    // Check if all fibers have completed
    bool IsDone() const noexcept;

    // Get the current scheduler (must be called from within a fiber)
    static Scheduler& Current();

    // Configuration
    struct Config {
        std::size_t default_stack_size = 262144;
        MemoryResourceSharedPtr memory_resource = GetDefaultMemoryResource();
    };
};

} // namespace cortex::tiny_fiber
```

#### 1b. Namespace Alias (Optional Convenience)

```cpp
// Users can optionally use shorter alias
namespace tf = cortex::tiny_fiber;

} // namespace cortex::fiber
```

#### 2. Fiber Handle & Future

When spawning a fiber, you get a `Future<T>` that can be waited on or its result retrieved.

```cpp
namespace cortex::tiny_fiber {

// Result type for fibers that return values
template <typename T>
class Future {
public:
    Future(Future&&) noexcept;
    Future& operator=(Future&&) noexcept;
    ~Future();

    // Block current fiber until this fiber completes (no return)
    void Wait();

    // Block current fiber until this fiber completes, return result
    T Get();

    // Check if the fiber has completed
    bool IsReady() const noexcept;
};

// Specialization for void - no Get(), only Wait()
template <>
class Future<void> {
public:
    Future(Future&&) noexcept;
    Future& operator=(Future&&) noexcept;
    ~Future();

    // Block current fiber until this fiber completes
    void Wait();

    // Check if the fiber has completed
    bool IsReady() const noexcept;
};

} // namespace cortex::tiny_fiber
```

#### 3. Spawning Fibers

```cpp
namespace cortex::tiny_fiber {

// Spawn a new fiber, returns Future to get result
template <typename F>
auto Spawn(F&& func) -> Future<std::invoke_result_t<F>>;

// Spawn with custom stack size
template <typename F>
auto Spawn(F&& func, std::size_t stack_size) -> Future<std::invoke_result_t<F>>;

} // namespace cortex::tiny_fiber
```

#### 4. Yielding Control

```cpp
namespace cortex::tiny_fiber {

// Yield control to other ready fibers
// Current fiber goes to back of ready queue
void Yield();

// Yield only if there are other ready fibers
// Returns true if yielded, false if no other fibers ready
bool YieldIfOthersReady();

} // namespace cortex::tiny_fiber
```

### Synchronization Primitives

#### 5. Cooperative Mutex

A mutex that yields instead of blocking the thread.

```cpp
namespace cortex::tiny_fiber {

class Mutex {
public:
    Mutex();
    ~Mutex();

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    // Lock the mutex, yields if already locked
    void Lock();

    // Try to lock without yielding
    bool TryLock();

    // Unlock the mutex
    void Unlock();

    // RAII lock guard
    class Guard {
    public:
        explicit Guard(Mutex& mutex);
        ~Guard();
        Guard(Guard&&) noexcept;
        Guard& operator=(Guard&&) noexcept;
    };
};

// Helper function
Mutex::Guard Lock(Mutex& mutex);

} // namespace cortex::tiny_fiber
```

#### 6. Cooperative Condition Variable

```cpp
namespace cortex::tiny_fiber {

class ConditionVariable {
public:
    ConditionVariable();
    ~ConditionVariable();

    // Wait until notified (must hold mutex)
    void Wait(Mutex::Guard& guard);

    // Wait with predicate
    template <typename Predicate>
    void Wait(Mutex::Guard& guard, Predicate pred);

    // Wake one waiting fiber
    void NotifyOne();

    // Wake all waiting fibers
    void NotifyAll();
};

} // namespace cortex::tiny_fiber
```

#### 7. Channel (Optional, can be added later)

```cpp
namespace cortex::tiny_fiber {

template <typename T>
class Channel {
public:
    explicit Channel(std::size_t capacity = 0);  // 0 = unbuffered

    // Send a value, yields if buffer is full
    void Send(T value);

    // Receive a value, yields if buffer is empty
    T Receive();

    // Try without yielding
    bool TrySend(T value);
    std::optional<T> TryReceive();

    // Close the channel
    void Close();
    bool IsClosed() const noexcept;
};

} // namespace cortex::tiny_fiber
```

---

## Usage Examples

### Example 1: Basic Spawning and Get

```cpp
#include <cortex/tiny_fiber.hpp>
#include <iostream>

int main() {
    cortex::tiny_fiber::Scheduler::Run([] {
        std::cout << "1";

        auto future = cortex::tiny_fiber::Spawn([] {
            std::cout << "->3";
            cortex::tiny_fiber::Yield();
            std::cout << "->5";
            return 42;  // Return a value
        });

        std::cout << "->2";
        cortex::tiny_fiber::Yield();
        std::cout << "->4";

        int result = future.Get();  // Wait for child, get result
        std::cout << "->6 (result=" << result << ")";
    });

    // Output: 1->2->3->4->5->6 (result=42)
    return 0;
}
```

### Example 2: Producer-Consumer with Mutex

```cpp
#include <cortex/tiny_fiber.hpp>
#include <queue>

int main() {
    cortex::tiny_fiber::Scheduler::Run([] {
        std::queue<int> buffer;
        cortex::tiny_fiber::Mutex mutex;
        cortex::tiny_fiber::ConditionVariable cv;
        bool done = false;

        // Producer (returns void, use Wait())
        auto producer = cortex::tiny_fiber::Spawn([&] {
            for (int i = 0; i < 5; ++i) {
                {
                    auto guard = cortex::tiny_fiber::Lock(mutex);
                    buffer.push(i);
                    cv.NotifyOne();
                }
                cortex::tiny_fiber::Yield();
            }
            auto guard = cortex::tiny_fiber::Lock(mutex);
            done = true;
            cv.NotifyAll();
        });

        // Consumer (returns void, use Wait())
        auto consumer = cortex::tiny_fiber::Spawn([&] {
            while (true) {
                auto guard = cortex::tiny_fiber::Lock(mutex);
                cv.Wait(guard, [&] { return !buffer.empty() || done; });
                
                if (buffer.empty() && done) break;
                
                int val = buffer.front();
                buffer.pop();
                std::cout << "Consumed: " << val << "\n";
            }
        });

        producer.Wait();  // Future<void> - only Wait(), no Get()
        consumer.Wait();
    });

    return 0;
}
```

### Example 3: Multiple Concurrent Tasks

```cpp
#include <cortex/tiny_fiber.hpp>
#include <vector>

int main() {
    cortex::tiny_fiber::Scheduler::Run([] {
        std::vector<cortex::tiny_fiber::Future<int>> futures;

        // Spawn 10 concurrent tasks
        for (int i = 0; i < 10; ++i) {
            futures.push_back(cortex::tiny_fiber::Spawn([i] {
                // Simulate work with multiple yields
                for (int j = 0; j < 3; ++j) {
                    std::cout << "Task " << i << " step " << j << "\n";
                    cortex::tiny_fiber::Yield();
                }
                return i * i;
            }));
        }

        // Get all results
        int sum = 0;
        for (auto& f : futures) {
            sum += f.Get();
        }
        std::cout << "Sum of squares: " << sum << "\n";
    });

    return 0;
}
```

### Example 4: WASM Integration with Step-based API

The `Scheduler::Create()` + `Step()` API allows clean integration with JavaScript's event loop:

```cpp
#include <cortex/tiny_fiber/tiny_fiber.hpp>
#include <emscripten.h>
#include <memory>

namespace tf = cortex::tiny_fiber;

namespace {
    std::unique_ptr<tf::Scheduler> g_scheduler;
}

// Start the workflow - creates scheduler
extern "C" EMSCRIPTEN_KEEPALIVE void start_workflow() {
    g_scheduler = std::make_unique<tf::Scheduler>(
        tf::Scheduler::Create([] {
            // Spawn fibers for game logic
            auto game = tf::Spawn([] {
                while (true) {
                    update_game_state();
                    tf::Yield();
                }
            });
            
            auto render = tf::Spawn([] {
                while (true) {
                    render_frame();
                    tf::Yield();
                }
            });
            
            game.Wait();
            render.Wait();
        })
    );
}

// Called from JS (via setInterval or requestAnimationFrame)
// Runs one fiber until it yields, then returns to JS
extern "C" EMSCRIPTEN_KEEPALIVE int step_workflow() {
    if (g_scheduler && !g_scheduler->IsDone()) {
        return g_scheduler->Step() ? 1 : 0;
    }
    return 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE int is_done() {
    return (!g_scheduler || g_scheduler->IsDone()) ? 1 : 0;
}
```

JavaScript integration:

```javascript
// Start the fiber workflow
Module.ccall('start_workflow', null, [], []);

// Step through at 60fps, allowing UI updates between steps
const stepInterval = setInterval(() => {
    if (Module.ccall('is_done', 'number', [], []) === 1) {
        clearInterval(stepInterval);
        return;
    }
    Module.ccall('step_workflow', null, [], []);
}, 16);  // ~60fps
```

---

## Implementation Strategy

### Phase 1: Core Infrastructure
1. `Scheduler` class with ready queue
2. `Fiber` internal wrapper around `cortex::Coroutine`
3. `Future<void>` for basic spawning
4. `Yield()` function

### Phase 2: Return Values
1. `Future<T>` with result storage
2. Exception propagation through `Await()`

### Phase 3: Synchronization
1. `Mutex` with wait queue
2. `ConditionVariable`
3. `Mutex::Guard` RAII wrapper

### Phase 4: Advanced Features (Optional)
1. `Channel<T>` for communication
2. `Select()` for waiting on multiple futures
3. `Timeout` support

---

## Internal Architecture

### Fiber States

```
    ┌─────────┐
    │  Ready  │ ◄─── Spawn() / Yield() / NotifyOne()
    └────┬────┘
         │ Scheduler picks
         ▼
    ┌─────────┐
    │ Running │ ◄─── Only one fiber at a time
    └────┬────┘
         │ Yield() / Await() / Lock() / Wait()
         ▼
    ┌─────────────┐
    │  Suspended  │ ◄─── Waiting for: Future / Mutex / CondVar
    └─────────────┘
         │ Dependency satisfied
         ▼
    ┌─────────┐
    │  Ready  │
    └─────────┘

    ┌──────────┐
    │ Finished │ ◄─── Body completed or exception
    └──────────┘
```

### Scheduler Loop

```cpp
class Scheduler {
    std::deque<Fiber*> ready_queue_;
    Fiber* current_fiber_ {nullptr};

    void RunLoop() {
        while (!ready_queue_.empty()) {
            current_fiber_ = ready_queue_.front();
            ready_queue_.pop_front();
            
            current_fiber_->Resume();
            
            if (current_fiber_->IsDone()) {
                // Fiber finished, wake up waiters
                current_fiber_->NotifyWaiters();
            }
            // else: fiber yielded or suspended, may be re-added to ready_queue_
        }
    }
};
```

---

## Open Questions

### 1. Naming

**Decision:** `cortex::tiny_fiber` - matches the module name

### 2. `Future<T>` destructor behavior

**Decision:** Destructor waits for fiber completion (blocks).
- No `Detach()` method - all fibers must be waited on
- Prevents orphaned fibers and resource leaks
- If you don't want to wait, you must explicitly call `Wait()` or `Get()` before destruction

### 3. Exception handling strategy?

When a fiber throws an uncaught exception:
- Store it in Future
- Rethrow when `Await()` is called
- If detached, log and continue? Or terminate?

**Recommendation:** Rethrow on `Get()` / `Wait()`, or in destructor if never waited

### 4. Should we provide `Scheduler::RunOneStep()` for WASM integration?

This allows integrating with JavaScript's event loop:

```cpp
// In WASM, instead of blocking:
while (!scheduler.IsDone()) {
    scheduler.RunOneStep();
    emscripten_sleep(0);  // Yield to JS event loop
}
```

**Recommendation:** Yes, provide this for WASM flexibility

### 5. Thread-local scheduler or explicit passing?

**Option A:** Thread-local current scheduler (implicit)
```cpp
cortex::fiber::Yield();  // Uses thread-local scheduler
```

**Option B:** Explicit scheduler handle
```cpp
scheduler.Yield();  // Explicit
```

**Recommendation:** Option A for ergonomics, with `Scheduler::Current()` escape hatch

---

## File Structure

```
include/cortex/tiny_fiber/
├── scheduler.hpp      # Scheduler class
├── future.hpp         # Future<T> template
├── yield.hpp          # Yield functions
├── mutex.hpp          # Cooperative Mutex
├── condition_variable.hpp  # Cooperative CondVar
├── channel.hpp        # Channel<T> (optional)
└── tiny_fiber.hpp     # Convenience header (includes all)

src/tiny_fiber/
├── scheduler.cpp
├── future.cpp
├── mutex.cpp
├── condition_variable.cpp
└── channel.cpp (optional)

tests/
├── tiny_fiber_scheduler_test.cpp
├── tiny_fiber_future_test.cpp
├── tiny_fiber_mutex_test.cpp
├── tiny_fiber_condvar_test.cpp
└── tiny_fiber_channel_test.cpp (optional)
```

---

## Alternatives Considered

### 1. Use C++20 coroutines (`co_await`)

**Pros:** Standard, stackless (efficient)
**Cons:** Limited Emscripten support, complex machinery, not stackful

**Decision:** Stick with stackful coroutines for WASM compatibility

### 2. Integrate with existing libraries (folly::coro, cppcoro)

**Pros:** Mature, well-tested
**Cons:** Heavy dependencies, may not support WASM

**Decision:** Build minimal implementation on cortex::Coroutine

### 3. Provide async/await syntax macros

```cpp
FIBER_ASYNC(int, ComputeAsync, (int x)) {
    FIBER_AWAIT(DoSomething());
    return x * 2;
}
```

**Decision:** Defer to future work; start with explicit API

---

## Summary

This RFC proposes adding a `cortex::tiny_fiber` module that provides:

| Component | Purpose |
|-----------|---------|
| `Scheduler` | Manages fiber execution |
| `Future<T>` | Result handle with `Wait()` and `Get()` |
| `Future<void>` | Handle with only `Wait()` |
| `Spawn()` | Create new fibers |
| `Yield()` | Cooperative yielding |
| `Mutex` | Cooperative locking |
| `ConditionVariable` | Cooperative waiting |
| `Channel<T>` | (Optional) Inter-fiber communication |

The implementation builds entirely on `cortex::Coroutine`, requires no threading, and works seamlessly on both native and WASM platforms.

---

## Feedback Requested

1. Is the proposed API ergonomic enough?
2. Should `Channel<T>` be in Phase 1 or deferred?
3. Preferred behavior for detached fiber exceptions?
4. Any missing primitives (Semaphore, Barrier, etc.)?
5. Alternative naming suggestions?

---

*Author: [Your Name]*
*Date: 2026-01-31*
*Status: Implemented*
