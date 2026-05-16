#include <video_editor/uploaded_source.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>

namespace {

using cortex::video_editor::UploadedFrameSource;

TEST(UploadedFrameSourceTest, ConstructsZeroInitialised) {
    UploadedFrameSource src(4, 2, 3);
    EXPECT_EQ(src.Width(), 4);
    EXPECT_EQ(src.Height(), 2);
    EXPECT_EQ(src.FrameCount(), 3);
    for (int i = 0; i < src.FrameCount(); ++i) {
        const auto& fb = src.At(i);
        EXPECT_EQ(fb.Width(), 4);
        EXPECT_EQ(fb.Height(), 2);
        ASSERT_EQ(fb.SizeBytes(), static_cast<std::size_t>(4 * 2 * 4));
        for (std::size_t b = 0; b < fb.SizeBytes(); ++b) {
            EXPECT_EQ(fb.Data()[b], 0) << "frame " << i << " byte " << b;
        }
    }
}

TEST(UploadedFrameSourceTest, WritablePixelsRoundtripViaPointer) {
    UploadedFrameSource src(2, 1, 2);
    auto* p0 = src.WritablePixels(0);
    auto* p1 = src.WritablePixels(1);
    ASSERT_NE(p0, nullptr);
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p0, p1) << "different frames should expose distinct buffers";

    // Frame 0: two opaque red pixels.
    p0[0] = 255;
    p0[1] = 0;
    p0[2] = 0;
    p0[3] = 255;
    p0[4] = 200;
    p0[5] = 10;
    p0[6] = 10;
    p0[7] = 255;
    // Frame 1: two opaque green pixels.
    p1[0] = 0;
    p1[1] = 255;
    p1[2] = 0;
    p1[3] = 255;
    p1[4] = 10;
    p1[5] = 200;
    p1[6] = 10;
    p1[7] = 255;

    const auto& fb0 = src.At(0);
    EXPECT_EQ(fb0.Data()[0], 255);
    EXPECT_EQ(fb0.Data()[1], 0);
    EXPECT_EQ(fb0.Data()[6], 10);
    EXPECT_EQ(fb0.Data()[7], 255);

    const auto& fb1 = src.At(1);
    EXPECT_EQ(fb1.Data()[1], 255);
    EXPECT_EQ(fb1.Data()[5], 200);
}

TEST(UploadedFrameSourceTest, AtThrowsForOutOfRangeIndex) {
    UploadedFrameSource src(1, 1, 2);
    EXPECT_THROW((void)src.At(-1), std::out_of_range);
    EXPECT_THROW((void)src.At(2), std::out_of_range);
}

TEST(UploadedFrameSourceTest, WritablePixelsReturnsNullForOutOfRange) {
    UploadedFrameSource src(1, 1, 2);
    EXPECT_EQ(src.WritablePixels(-1), nullptr);
    EXPECT_EQ(src.WritablePixels(2), nullptr);
}

TEST(UploadedFrameSourceTest, ThrowsForNonPositiveDimensions) {
    EXPECT_THROW(UploadedFrameSource(0, 1, 1), std::invalid_argument);
    EXPECT_THROW(UploadedFrameSource(1, 0, 1), std::invalid_argument);
    EXPECT_THROW(UploadedFrameSource(1, 1, 0), std::invalid_argument);
}

} // namespace
