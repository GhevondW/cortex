#pragma once

#include <video_editor/frame_source.hpp>

#include <cstdint>
#include <vector>

namespace cortex::video_editor {

// Frame source backed by client-supplied pixel data. Construction allocates
// (width * height * 4 * frame_count) bytes zero-initialised; callers populate
// each frame in place by writing RGBA8 into the pointer returned by
// WritablePixels(idx). Used by the browser's "Upload video" flow: JS decodes
// frames via HTMLVideoElement + <canvas>.getImageData, then copies each
// frame's RGBA bytes into the buffer through the WASM heap.
class UploadedFrameSource final : public IFrameSource {
public:
    UploadedFrameSource(int width, int height, int frame_count);

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

    // Returns a pointer to frame idx's RGBA8 pixel buffer for in-place
    // population, or nullptr if idx is out of range. The pointed-to region is
    // exactly (Width() * Height() * 4) bytes wide.
    [[nodiscard]] std::uint8_t* WritablePixels(int idx) noexcept;

private:
    int width_;
    int height_;
    std::vector<FrameBuffer> frames_;
};

} // namespace cortex::video_editor
