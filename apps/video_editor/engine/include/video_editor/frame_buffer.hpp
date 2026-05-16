#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cortex::video_editor {

// Owns an RGBA8 pixel buffer of fixed width × height. Stride is implicit
// (width * 4). All filters consume and produce FrameBuffers of identical
// dimensions; no resizing is performed inside the editor.
class FrameBuffer final {
public:
    FrameBuffer() = default;
    FrameBuffer(int width, int height);

    FrameBuffer(const FrameBuffer&) = default;
    FrameBuffer(FrameBuffer&&) noexcept = default;
    FrameBuffer& operator=(const FrameBuffer&) = default;
    FrameBuffer& operator=(FrameBuffer&&) noexcept = default;

    [[nodiscard]] int Width() const noexcept {
        return width_;
    }

    [[nodiscard]] int Height() const noexcept {
        return height_;
    }

    [[nodiscard]] std::size_t SizeBytes() const noexcept {
        return pixels_.size();
    }

    [[nodiscard]] std::uint8_t* Data() noexcept {
        return pixels_.data();
    }

    [[nodiscard]] const std::uint8_t* Data() const noexcept {
        return pixels_.data();
    }

    // Copies pixel data from another buffer with identical dimensions.
    void CopyFrom(const FrameBuffer& other);

    // Fills the buffer with a solid RGBA color (used by tests).
    void Fill(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a);

private:
    int width_ {0};
    int height_ {0};
    std::vector<std::uint8_t> pixels_;
};

} // namespace cortex::video_editor
