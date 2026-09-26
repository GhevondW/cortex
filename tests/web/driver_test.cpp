// Scenarios for js/cortex.mjs, exported to tests/web/driver_test.mjs. Each
// start_* function creates a scheduler and returns it for the JS driver.

#include <cortex/tiny_fiber/tiny_fiber.hpp>

#include <emscripten.h>

#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace tf = cortex::tiny_fiber;
using namespace std::chrono_literals;

// clang-format off
EM_JS(void, throw_from_js, (), {
    throw new Error("js boom");
});
// clang-format on

namespace {

std::vector<std::unique_ptr<tf::Scheduler>> g_schedulers;
std::optional<tf::Promise<int>> g_promise;
int g_promise_result = 0;

void* Keep(std::unique_ptr<tf::Scheduler> scheduler) {
    g_schedulers.push_back(std::move(scheduler));
    return g_schedulers.back().get();
}

} // namespace

extern "C" {

// Three 20 ms sleeps in a row.
EMSCRIPTEN_KEEPALIVE void* start_sleeps() {
    return Keep(tf::Scheduler::Create([] {
        for (int i = 0; i < 3; ++i) {
            tf::SleepFor(20ms);
        }
    }));
}

// A 150 ms busy computation that only calls CheckPoint().
EMSCRIPTEN_KEEPALIVE void* start_busy_checkpoint() {
    return Keep(tf::Scheduler::Create([] {
        const auto end = std::chrono::steady_clock::now() + 150ms;
        volatile unsigned long sink = 0;
        while (std::chrono::steady_clock::now() < end) {
            for (int i = 0; i < 1000; ++i) {
                sink = sink + static_cast<unsigned long>(i);
            }
            tf::CheckPoint();
        }
    }));
}

// A fiber that waits on a condition nobody signals.
EMSCRIPTEN_KEEPALIVE void* start_deadlock() {
    return Keep(tf::Scheduler::Create([] {
        tf::SetFiberName("stuck");
        tf::Mutex mutex;
        tf::ConditionVariable cv;
        auto guard = tf::Lock(mutex);
        cv.Wait(guard);
    }));
}

// A fiber that throws.
EMSCRIPTEN_KEEPALIVE void* start_throwing() {
    return Keep(tf::Scheduler::Create([] {
        tf::Yield();
        throw std::runtime_error("boom from fiber");
    }));
}

// A fiber that awaits a promise JavaScript fulfils later.
EMSCRIPTEN_KEEPALIVE void* start_promise() {
    return Keep(tf::Scheduler::Create([] {
        g_promise.emplace();
        auto future = g_promise->GetFuture();
        g_promise_result = future.Get();
    }));
}

// A fiber that traps, like C++ crashing (a WebAssembly `unreachable`).
EMSCRIPTEN_KEEPALIVE void* start_trap() {
    return Keep(tf::Scheduler::Create([] {
        tf::Yield();
        __builtin_trap();
    }));
}

// A fiber whose JavaScript call throws: a JavaScript exception, not a C++ one,
// unwinds the fiber.
EMSCRIPTEN_KEEPALIVE void* start_js_throw() {
    return Keep(tf::Scheduler::Create([] {
        tf::Yield();
        throw_from_js();
    }));
}

// A fiber that sleeps far longer than any test runs.
EMSCRIPTEN_KEEPALIVE void* start_long_sleep() {
    return Keep(tf::Scheduler::Create([] {
        tf::SleepFor(1h);
    }));
}

EMSCRIPTEN_KEEPALIVE void resolve_promise(int value) {
    g_promise->SetValue(value);
}

EMSCRIPTEN_KEEPALIVE int promise_result() {
    return g_promise_result;
}

EMSCRIPTEN_KEEPALIVE void destroy_scheduler(void* scheduler) {
    std::erase_if(g_schedulers, [scheduler](const auto& owned) {
        return owned.get() == scheduler;
    });
}

} // extern "C"

int main() {
    return 0;
}
