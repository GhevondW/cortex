// Emscripten backend: suspending a coroutine saves the WebAssembly locals of
// every frame on its stack into the coroutine's Asyncify buffer. A deep
// recursion that suspends at the bottom (e.g. a backtracking search that
// yields to the browser) must fit.

#include <cortex/coroutine.hpp>

#include <gtest/gtest.h>

#include <vector>

namespace {

// Keeps several values live across the recursive call, so every frame has
// locals that Asyncify must save when the coroutine suspends at the bottom.
// Recording each frame's result after the call keeps the compiler from
// turning the recursion into a loop.
int Descend(int depth, cortex::CoroutineSuspendContext& ctx, std::vector<int>& trail) {
    if (depth == 0) {
        ctx.Suspend();
        return 0;
    }
    const int a = depth * 3;
    const int b = depth ^ 0x5a;
    const int c = depth % 7;
    const int below = Descend(depth - 1, ctx, trail);
    const int here = below + a - b + c;
    trail.push_back(here);
    return here;
}

int Expected(int depth) {
    int total = 0;
    for (int d = 1; d <= depth; ++d) {
        total += d * 3 - (d ^ 0x5a) + d % 7;
    }
    return total;
}

} // namespace

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define CORTEX_TEST_ASAN 1
#endif
#endif

TEST(WasmAsyncify, DeepRecursionCanSuspendAtTheBottom) {
#ifdef CORTEX_TEST_ASAN
    // AddressSanitizer gives every frame more locals to save: 1000 frames of
    // Descend() fit in the default 64 KB buffer, 1500 do not.
    constexpr int kDepth = 1000;
#else
    constexpr int kDepth = 2000;
#endif
    int result = -1;
    std::vector<int> trail;
    auto coroutine = cortex::Coroutine::Make(
        [&](cortex::CoroutineSuspendContext& ctx) {
            result = Descend(kDepth, ctx, trail);
        },
        1024 * 1024);

    coroutine.Resume(); // runs down to the bottom and suspends there
    EXPECT_FALSE(coroutine.IsDone());
    coroutine.Resume(); // unwinds all the way back up
    EXPECT_TRUE(coroutine.IsDone());
    EXPECT_EQ(result, Expected(kDepth));
    EXPECT_EQ(trail.size(), static_cast<std::size_t>(kDepth));
}
