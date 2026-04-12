#include <memory>

#include <cortex/config.hpp>
#include <cortex/coroutine.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// Bridge to call a JS function named 'handleStep'
// clang-format off
EM_JS(void, call_js_step, (int value), {
    if (typeof handleStep === 'function') {
        handleStep(value);
    } else {
        console.log("JS: step", value);
    }
});
// clang-format on
#endif

namespace {
std::unique_ptr<cortex::Coroutine> global_coro;
}

extern "C" {

CORTEX_API void start_coroutine_example(int iterations) {
    auto coro = cortex::Coroutine::Make([iterations](cortex::CoroutineSuspendContext& ctx) {
        for (int i = 1; i <= iterations; ++i) {
            call_js_step(i);
            ctx.Suspend();
        }
    });

    global_coro = std::make_unique<cortex::Coroutine>(std::move(coro));

    global_coro->Resume();
}

CORTEX_API void resume_coroutine_example() {
    if (global_coro && !global_coro->IsDone()) {
        global_coro->Resume();
    }
}

CORTEX_API int is_coroutine_done() {
    return (global_coro && global_coro->IsDone()) ? 1 : 0;
}
}

int main() {
    return 0;
}
