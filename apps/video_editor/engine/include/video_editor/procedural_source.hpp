#pragma once

#include <video_editor/frame_source.hpp>

#include <vector>

namespace cortex::video_editor {

// Generates an animated synthetic "video" into a fixed array of FrameBuffers.
// All frames are produced at construction time and stored in memory; no I/O,
// no codecs, no decoding cost during editing. Content is deterministic in
// (frame_idx, x, y) — useful for tests and reproducible demos.
//
// Content recipe per frame:
//   * Time-varying RGB gradient (sin/cos compositions of frame_idx)
//   * Three colored discs that orbit / drift around the frame
class ProceduralFrameSource final : public IFrameSource {
public:
    ProceduralFrameSource(int width, int height, int frame_count);

    [[nodiscard]] int Width() const noexcept override {
        return width_;
    }
    [[nodiscard]] int Height() const noexcept override {
        return height_;
    }
    [[nodiscard]] int FrameCount() const noexcept override {
        return static_cast<int>(frames_.size());
    }
    [[nodiscard]] const FrameBuffer& At(int idx) const override;

private:
    void RenderFrame(int idx, FrameBuffer& out) const;

    int width_;
    int height_;
    std::vector<FrameBuffer> frames_;
};

} // namespace cortex::video_editor
