// Validates the live-editor engine contract: initialise an uploaded source with
// a SINGLE frame, write pixels into slot 0, run the filter chain on it, and read
// the filtered result back from output slot 0. This is the exact round trip the
// real-time editor performs once per displayed video frame, so it must hold
// before any JS is wired up.

#include <video_editor/editor.hpp>

#include <gtest/gtest.h>

#include <cstdint>

namespace {

using cortex::video_editor::Editor;

TEST(LiveSingleFrame, BrightnessRoundTripsThroughSlotZero) {
    auto editor = Editor::Create(2, 1, 1);
    ASSERT_NE(editor, nullptr);

    editor->ResetUploaded(2, 1, 1);
    std::uint8_t* px = editor->WritableSourcePixels(0);
    ASSERT_NE(px, nullptr);
    // Two mid-grey pixels, fully opaque.
    for (int i = 0; i < 2 * 1 * 4; ++i) {
        px[i] = (i % 4 == 3) ? 255 : 100;
    }

    editor->SetBrightness(0.5f); // +0.5 → +127.5 before clamp → 100 becomes 227
    editor->RenderPreview(0);

    const auto& out = editor->Output(0);
    const std::uint8_t* o = out.Data();
    EXPECT_EQ(o[0], 227);
    EXPECT_EQ(o[1], 227);
    EXPECT_EQ(o[2], 227);
    EXPECT_EQ(o[3], 255); // alpha preserved
}

TEST(LiveSingleFrame, WritableSourcePixelsNullWhenProcedural) {
    auto editor = Editor::Create(4, 4, 3);
    // Default source is procedural → not writable.
    EXPECT_EQ(editor->WritableSourcePixels(0), nullptr);
}

TEST(LiveSingleFrame, ResetUploadedToSingleFrameReportsOneFrame) {
    auto editor = Editor::Create(8, 8, 60); // start large + procedural
    editor->ResetUploaded(4, 4, 1);
    EXPECT_EQ(editor->Width(), 4);
    EXPECT_EQ(editor->Height(), 4);
    EXPECT_EQ(editor->FrameCount(), 1);
    EXPECT_NE(editor->WritableSourcePixels(0), nullptr);
    EXPECT_EQ(editor->WritableSourcePixels(1), nullptr); // out of range
}

} // namespace
