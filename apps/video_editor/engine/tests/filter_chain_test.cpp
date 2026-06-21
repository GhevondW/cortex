#include <video_editor/filter_chain.hpp>
#include <video_editor/filters/brightness.hpp>
#include <video_editor/filters/contrast.hpp>

#include <gtest/gtest.h>

using namespace cortex::video_editor;
using namespace cortex::video_editor::filters;

namespace {

FrameBuffer MakeSolid(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    FrameBuffer fb(w, h);
    fb.Fill(r, g, b, 255);
    return fb;
}

} // namespace

TEST(FilterChainTest, EmptyChainCopiesInput) {
    auto in = MakeSolid(3, 3, 10, 20, 30);
    FrameBuffer out(3, 3);
    FilterChain chain;
    chain.Apply(in, out);
    EXPECT_EQ(out.Data()[0], 10);
    EXPECT_EQ(out.Data()[1], 20);
    EXPECT_EQ(out.Data()[2], 30);
}

TEST(FilterChainTest, SingleFilterWritesDirectlyToOutput) {
    auto in = MakeSolid(2, 2, 100, 100, 100);
    FrameBuffer out(2, 2);
    FilterChain chain;
    chain.Add(std::make_unique<BrightnessFilter>(0.2f));
    chain.Apply(in, out);
    EXPECT_EQ(out.Data()[0], 151);
}

TEST(FilterChainTest, OppositeBrightnessShiftsApproximatelyCancel) {
    // Brightness is byte-quantized per stage; ±0.1 doesn't round-trip exactly
    // but should stay within a unit of the source value.
    auto in = MakeSolid(2, 2, 80, 120, 160);
    FrameBuffer out(2, 2);
    FilterChain chain;
    chain.Add(std::make_unique<BrightnessFilter>(0.1f));
    chain.Add(std::make_unique<BrightnessFilter>(-0.1f));
    chain.Apply(in, out);
    EXPECT_NEAR(out.Data()[0], 80, 1);
    EXPECT_NEAR(out.Data()[1], 120, 1);
    EXPECT_NEAR(out.Data()[2], 160, 1);
}

TEST(FilterChainTest, OrderMattersForContrastThenBrightness) {
    auto in = MakeSolid(2, 2, 200, 200, 200);
    FrameBuffer out_a(2, 2);
    FrameBuffer out_b(2, 2);

    // Contrast then brightness vs brightness then contrast: should differ for
    // a non-midgray input.
    FilterChain a;
    a.Add(std::make_unique<ContrastFilter>(1.5f));
    a.Add(std::make_unique<BrightnessFilter>(0.05f));
    a.Apply(in, out_a);

    FilterChain b;
    b.Add(std::make_unique<BrightnessFilter>(0.05f));
    b.Add(std::make_unique<ContrastFilter>(1.5f));
    b.Apply(in, out_b);

    EXPECT_NE(out_a.Data()[0], out_b.Data()[0]);
}

TEST(FilterChainTest, ClearRestoresEmptyBehavior) {
    auto in = MakeSolid(2, 2, 50, 60, 70);
    FrameBuffer out(2, 2);
    FilterChain chain;
    chain.Add(std::make_unique<BrightnessFilter>(0.5f));
    chain.Clear();
    chain.Apply(in, out);
    EXPECT_EQ(out.Data()[0], 50);
}
