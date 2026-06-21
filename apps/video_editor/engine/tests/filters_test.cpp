#include <video_editor/filters/brightness.hpp>
#include <video_editor/filters/contrast.hpp>
#include <video_editor/filters/gaussian_blur.hpp>
#include <video_editor/filters/saturation.hpp>

#include <gtest/gtest.h>

using namespace cortex::video_editor;
using namespace cortex::video_editor::filters;

namespace {

FrameBuffer MakeSolid(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
    FrameBuffer fb(w, h);
    fb.Fill(r, g, b, a);
    return fb;
}

std::uint8_t Pixel(const FrameBuffer& fb, int x, int y, int channel) {
    const std::size_t off =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(fb.Width()) + static_cast<std::size_t>(x)) * 4u +
        static_cast<std::size_t>(channel);
    return fb.Data()[off];
}

} // namespace

TEST(FiltersTest, BrightnessZeroIsIdentity) {
    auto in = MakeSolid(4, 4, 100, 150, 200);
    FrameBuffer out(4, 4);
    BrightnessFilter f(0.0f);
    f.Apply(in, out);
    EXPECT_EQ(Pixel(out, 0, 0, 0), 100);
    EXPECT_EQ(Pixel(out, 0, 0, 1), 150);
    EXPECT_EQ(Pixel(out, 0, 0, 2), 200);
}

TEST(FiltersTest, BrightnessPositiveLightens) {
    auto in = MakeSolid(2, 2, 100, 100, 100);
    FrameBuffer out(2, 2);
    BrightnessFilter f(0.2f); // +51
    f.Apply(in, out);
    EXPECT_EQ(Pixel(out, 0, 0, 0), 151);
}

TEST(FiltersTest, BrightnessSaturatesAtMax) {
    auto in = MakeSolid(2, 2, 250, 250, 250);
    FrameBuffer out(2, 2);
    BrightnessFilter f(1.0f); // +255 clamps to 255
    f.Apply(in, out);
    EXPECT_EQ(Pixel(out, 0, 0, 0), 255);
}

TEST(FiltersTest, ContrastOneIsIdentity) {
    auto in = MakeSolid(2, 2, 64, 128, 200);
    FrameBuffer out(2, 2);
    ContrastFilter f(1.0f);
    f.Apply(in, out);
    EXPECT_EQ(Pixel(out, 0, 0, 0), 64);
    EXPECT_EQ(Pixel(out, 0, 0, 1), 128);
    EXPECT_EQ(Pixel(out, 0, 0, 2), 200);
}

TEST(FiltersTest, ContrastZeroFlattensToMidGray) {
    auto in = MakeSolid(2, 2, 0, 128, 255);
    FrameBuffer out(2, 2);
    ContrastFilter f(0.0f);
    f.Apply(in, out);
    EXPECT_EQ(Pixel(out, 0, 0, 0), 128);
    EXPECT_EQ(Pixel(out, 0, 0, 1), 128);
    EXPECT_EQ(Pixel(out, 0, 0, 2), 128);
}

TEST(FiltersTest, SaturationOneIsIdentity) {
    auto in = MakeSolid(2, 2, 200, 100, 50);
    FrameBuffer out(2, 2);
    SaturationFilter f(1.0f);
    f.Apply(in, out);
    EXPECT_EQ(Pixel(out, 0, 0, 0), 200);
    EXPECT_EQ(Pixel(out, 0, 0, 1), 100);
    EXPECT_EQ(Pixel(out, 0, 0, 2), 50);
}

TEST(FiltersTest, SaturationZeroProducesGrayscale) {
    auto in = MakeSolid(2, 2, 200, 100, 50);
    FrameBuffer out(2, 2);
    SaturationFilter f(0.0f);
    f.Apply(in, out);
    // luma = 0.299*200 + 0.587*100 + 0.114*50 ≈ 124
    const auto r = Pixel(out, 0, 0, 0);
    EXPECT_EQ(r, Pixel(out, 0, 0, 1));
    EXPECT_EQ(r, Pixel(out, 0, 0, 2));
    EXPECT_GE(r, 120);
    EXPECT_LE(r, 130);
}

TEST(FiltersTest, GaussianBlurRadiusZeroIsIdentity) {
    auto in = MakeSolid(4, 4, 80, 160, 240);
    // Mark center pixel
    in.Data()[(1 * 4 + 1) * 4 + 0] = 0;
    FrameBuffer out(4, 4);
    GaussianBlurFilter f(0);
    f.Apply(in, out);
    EXPECT_EQ(Pixel(out, 0, 0, 0), 80);
    EXPECT_EQ(Pixel(out, 1, 1, 0), 0);
}

TEST(FiltersTest, GaussianBlurSpreadsImpulse) {
    FrameBuffer in(7, 7);
    in.Fill(0, 0, 0, 255);
    // Single bright center pixel.
    const std::size_t center = (3 * 7 + 3) * 4;
    in.Data()[center + 0] = 255;
    in.Data()[center + 1] = 255;
    in.Data()[center + 2] = 255;

    FrameBuffer out(7, 7);
    GaussianBlurFilter f(2);
    f.Apply(in, out);

    // The center should still be the brightest pixel.
    const auto center_val = Pixel(out, 3, 3, 0);
    // Adjacent pixels should be brighter than zero (the impulse spread).
    EXPECT_GT(Pixel(out, 2, 3, 0), 0);
    EXPECT_GT(Pixel(out, 3, 2, 0), 0);
    EXPECT_GT(Pixel(out, 4, 3, 0), 0);
    EXPECT_GT(Pixel(out, 3, 4, 0), 0);
    // Pixels far from center should be dimmer than the center.
    EXPECT_LT(Pixel(out, 0, 0, 0), center_val);
}

TEST(FiltersTest, GaussianBlurPreservesAlpha) {
    auto in = MakeSolid(5, 5, 200, 200, 200, 137);
    FrameBuffer out(5, 5);
    GaussianBlurFilter f(2);
    f.Apply(in, out);
    EXPECT_EQ(Pixel(out, 2, 2, 3), 137);
}
