#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>

#include <cortex/config.hpp>
#include <cortex/coroutine.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// Bridge functions to communicate with JavaScript
// clang-format off
EM_JS(void, js_update_progress, (int percent), {
    if (typeof updateProgress === 'function') {
        updateProgress(percent);
    }
});

EM_JS(void, js_update_status, (const char* msg), {
    if (typeof updateStatus === 'function') {
        updateStatus(UTF8ToString(msg));
    }
});

EM_JS(void, js_computation_complete, (double elapsed_ms), {
    if (typeof onComputationComplete === 'function') {
        onComputationComplete(elapsed_ms);
    }
});
// clang-format on
#endif

namespace {
std::unique_ptr<cortex::Coroutine> computation_coro;
bool use_coroutine = true;
int current_iteration = 0;
int total_iterations = 0;
std::chrono::steady_clock::time_point start_time;
} // namespace

// Simulates heavy image processing work
// This represents operations like blur, edge detection, color transformation, etc.
double heavy_computation_step(int iteration) {
    // Simulate complex operations on a virtual image
    double result = 0.0;

    // Simulate processing a 1000x1000 pixel image with multiple passes
    const int image_size = 1000;
    const int operations_per_iteration = 500;

    for (int op = 0; op < operations_per_iteration; ++op) {
        for (int i = 0; i < image_size; ++i) {
            // Simulate various mathematical operations
            // (convolution kernels, color space transformations, etc.)
            double x = static_cast<double>(i) / image_size;
            double y = static_cast<double>(iteration) / 100.0;

            // Simulate expensive trigonometric and exponential operations
            result += std::sin(x * y * 3.14159) * std::cos(x * x * y);
            result += std::exp(-x * x - y * y);
            result += std::sqrt(std::abs(result + 1.0));

            // Prevent compiler from optimizing away
            if (result > 1e10) result = std::fmod(result, 1e9);
        }
    }

    return result;
}

// WITH COROUTINES: This function performs the EXACT SAME computation as the non-coroutine
// version below. The ONLY difference is ctx.Suspend() which yields control to the browser.
// This is a 100% fair comparison - no artificial delays or tricks!
void run_computation_with_coroutines(int iterations) {
    total_iterations = iterations;
    current_iteration = 0;
    start_time = std::chrono::steady_clock::now();

    computation_coro =
        std::make_unique<cortex::Coroutine>(cortex::Coroutine::Make([iterations](cortex::CoroutineSuspendContext& ctx) {
            std::cout << "[C++] Starting computation with coroutines (" << iterations << " iterations)\n";

#ifdef __EMSCRIPTEN__
            js_update_status("Processing with Coroutines...");
#endif

            double result_sum = 0.0;

            for (int i = 0; i < iterations; ++i) {
                current_iteration = i;

                // Perform EXACT SAME heavy computation as non-coroutine version
                result_sum += heavy_computation_step(i);

                // Update progress
                int percent = static_cast<int>((i + 1) * 100 / iterations);
#ifdef __EMSCRIPTEN__
                js_update_progress(percent);
#endif

                if (i % 5 == 0) {
                    std::cout << "[C++] Progress: " << percent << "%\n";
                }

                // Suspend to allow UI updates - this is the key advantage!
                // Without this suspend, the browser would freeze
                ctx.Suspend();
            }

            auto end_time = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

            std::cout << "[C++] Computation complete! Result sum: " << result_sum << ", Time: " << elapsed << "ms\n";

#ifdef __EMSCRIPTEN__
            js_update_status("Complete!");
            js_computation_complete(static_cast<double>(elapsed));
#endif
        }));

    // Start the coroutine
    computation_coro->Resume();
}

// WITHOUT COROUTINES: This function performs the EXACT SAME computation as the coroutine
// version above. The ONLY difference is NO ctx.Suspend() call, which causes the browser
// to freeze naturally (not artificially). This is a 100% fair comparison!
void run_computation_without_coroutines(int iterations) {
    start_time = std::chrono::steady_clock::now();

    std::cout << "[C++] Starting computation WITHOUT coroutines (" << iterations << " iterations)\n";

#ifdef __EMSCRIPTEN__
    js_update_status("Processing WITHOUT Coroutines (UI will freeze)...");
#endif

    double result_sum = 0.0;

    // This will block the entire browser UI naturally (single-threaded event loop)!
    for (int i = 0; i < iterations; ++i) {
        // Perform EXACT SAME heavy computation as coroutine version
        result_sum += heavy_computation_step(i);

        int percent = static_cast<int>((i + 1) * 100 / iterations);
#ifdef __EMSCRIPTEN__
        js_update_progress(percent);
#endif

        if (i % 5 == 0) {
            std::cout << "[C++] Progress: " << percent << "%\n";
        }

        // NO SUSPEND - UI will be frozen!
    }

    auto end_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    std::cout << "[C++] Computation complete! Result sum: " << result_sum << ", Time: " << elapsed << "ms\n";

#ifdef __EMSCRIPTEN__
    js_update_status("Complete!");
    js_computation_complete(static_cast<double>(elapsed));
#endif
}

extern "C" {

CORTEX_API void start_computation(int use_coro, int iterations) {
    use_coroutine = (use_coro != 0);

    if (use_coroutine) {
        run_computation_with_coroutines(iterations);
    } else {
        run_computation_without_coroutines(iterations);
    }
}

CORTEX_API void resume_computation() {
    if (computation_coro && !computation_coro->IsDone()) {
        computation_coro->Resume();
    }
}

CORTEX_API int is_computation_done() {
    return (!computation_coro || computation_coro->IsDone()) ? 1 : 0;
}

CORTEX_API int get_current_iteration() {
    return current_iteration;
}

CORTEX_API int get_total_iterations() {
    return total_iterations;
}
}

int main() {
    std::cout << "Cortex Particle Simulation Demo Ready\n";
    return 0;
}
