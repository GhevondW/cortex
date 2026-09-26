// The cooperative single-frame renderer runs the real filters in a fiber; they
// call tiny_fiber::CheckPoint() per row, which yields once the time slice is
// spent. Output must be byte-identical to the synchronous FilterChain for the
// same parameters, whatever the slice.

#include <video_editor/filter_chain.hpp>
#include <video_editor/filters/brightness.hpp>
#include <video_editor/filters/contrast.hpp>
#include <video_editor/filters/gaussian_blur.hpp>
#include <video_editor/filters/saturation.hpp>
#include <video_editor/frame_buffer.hpp>
#include <video_editor/live_cooperative.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>

namespace {

using namespace cortex::video_editor;
using namespace std::chrono_literals;

FrameBuffer MakeNoise(int w, int h, std::uint32_t seed) {
    FrameBuffer fb(w, h);
    std::uint8_t* p = fb.Data();
    std::uint32_t s = seed ? seed : 1u;
    const int n = w * h * 4;
    for (int i = 0; i < n; ++i) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        p[i] = (i % 4 == 3) ? 255 : static_cast<std::uint8_t>(s & 0xFFu);
    }
    return fb;
}

// Build the synchronous chain the same way Editor::RebuildChain does (identity
// stages skipped) and apply it.
FrameBuffer RunSync(const FrameBuffer& src, const LiveFilterParams& p) {
    FilterChain chain;
    if (p.brightness != 0.0f) {
        chain.Add(std::make_unique<filters::BrightnessFilter>(p.brightness));
    }
    if (p.contrast != 1.0f) {
        chain.Add(std::make_unique<filters::ContrastFilter>(p.contrast));
    }
    if (p.saturation != 1.0f) {
        chain.Add(std::make_unique<filters::SaturationFilter>(p.saturation));
    }
    if (p.blur_radius > 0) {
        chain.Add(std::make_unique<filters::GaussianBlurFilter>(p.blur_radius));
    }
    FrameBuffer out(src.Width(), src.Height());
    chain.Apply(src, out);
    return out;
}

// Renders cooperatively; returns the output and reports how many steps it took.
FrameBuffer RunCooperative(const FrameBuffer& src,
                           const LiveFilterParams& p,
                           std::chrono::microseconds time_slice,
                           int* steps_taken = nullptr) {
    LiveCooperativeRenderer r(time_slice);
    r.Begin(src, p);
    int steps = 1;
    while (r.Step()) {
        if (++steps > 1'000'000) {
            ADD_FAILURE() << "cooperative render did not converge";
            break;
        }
    }
    EXPECT_TRUE(r.Done());
    if (steps_taken != nullptr) {
        *steps_taken = steps;
    }
    return r.Output(); // FrameBuffer is copyable
}

void ExpectByteIdentical(const FrameBuffer& a, const FrameBuffer& b) {
    ASSERT_EQ(a.Width(), b.Width());
    ASSERT_EQ(a.Height(), b.Height());
    ASSERT_EQ(a.SizeBytes(), b.SizeBytes());
    EXPECT_EQ(0, std::memcmp(a.Data(), b.Data(), a.SizeBytes()));
}

TEST(LiveCooperative, MatchesSyncAcrossParamsAndSlices) {
    const int w = 37, h = 29; // odd dims
    const FrameBuffer src = MakeNoise(w, h, 0xC0FFEEu);

    const LiveFilterParams cases[] = {
        {}, // empty chain → copy
        {0.25f, 1.0f, 1.0f, 0}, // brightness only
        {0.0f, 1.4f, 1.0f, 0}, // contrast only
        {0.0f, 1.0f, 0.6f, 0}, // saturation only
        {0.0f, 1.0f, 1.0f, 5}, // blur only
        {-0.3f, 1.6f, 0.4f, 7}, // everything
        {0.1f, 0.8f, 1.5f, 3}, // everything, different signs
    };

    for (const auto& p : cases) {
        const FrameBuffer expected = RunSync(src, p);
        for (auto slice : {0us, 50us, 2000us}) {
            const FrameBuffer got = RunCooperative(src, p, slice);
            SCOPED_TRACE(testing::Message() << "brightness=" << p.brightness << " contrast=" << p.contrast
                                            << " saturation=" << p.saturation << " blur=" << p.blur_radius
                                            << " slice_us=" << slice.count());
            ExpectByteIdentical(got, expected);
        }
    }
}

// A zero slice makes every CheckPoint() yield: the real filters must be
// splitting their work (one checkpoint per row).
TEST(LiveCooperative, RealFiltersYieldAtCheckPoints) {
    const FrameBuffer src = MakeNoise(24, 24, 3u);
    int steps = 0;
    (void)RunCooperative(src, LiveFilterParams {0.1f, 1.2f, 0.8f, 3}, 0us, &steps);
    EXPECT_GT(steps, 24); // at least one yield per row of one pass
}

TEST(LiveCooperative, RunForFinishesAFrameWithinRepeatedBudgets) {
    const FrameBuffer src = MakeNoise(40, 30, 9u);
    const LiveFilterParams params {0.0f, 1.0f, 1.0f, 4};
    LiveCooperativeRenderer r(100us);
    r.Begin(src, params);
    int calls = 0;
    while (r.RunFor(200us)) {
        ASSERT_LT(++calls, 100000);
    }
    EXPECT_TRUE(r.Done());
    ExpectByteIdentical(r.Output(), RunSync(src, params));
}

TEST(LiveCooperative, ReBeginReusesRenderer) {
    const FrameBuffer src = MakeNoise(16, 16, 42u);
    LiveCooperativeRenderer r;

    r.Begin(src, LiveFilterParams {0.5f, 1.0f, 1.0f, 0});
    while (r.Step()) {
    }
    const FrameBuffer first = r.Output();
    ExpectByteIdentical(first, RunSync(src, LiveFilterParams {0.5f, 1.0f, 1.0f, 0}));

    r.Begin(src, LiveFilterParams {0.0f, 1.0f, 1.0f, 4});
    while (r.Step()) {
    }
    ExpectByteIdentical(r.Output(), RunSync(src, LiveFilterParams {0.0f, 1.0f, 1.0f, 4}));
}

TEST(LiveCooperative, DestroyingMidRenderIsClean) {
    const FrameBuffer src = MakeNoise(32, 32, 7u);
    LiveCooperativeRenderer r(0us); // yield at every checkpoint
    r.Begin(src, LiveFilterParams {-0.2f, 1.3f, 0.7f, 6});
    r.Step(); // run a couple of rows, then drop the renderer mid-render
    r.Step();
    SUCCEED(); // ~LiveCooperativeRenderer must tear the scheduler down cleanly
}

} // namespace
