#include <video_editor/frame_buffer.hpp>

#include <cassert>
#include <cstring>
#include <stdexcept>

namespace cortex::video_editor {

FrameBuffer::FrameBuffer(int width, int height)
    : width_(width)
    , height_(height)
    , pixels_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0) {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("FrameBuffer dimensions must be positive");
    }
}

void FrameBuffer::CopyFrom(const FrameBuffer& other) {
    assert(width_ == other.width_ && height_ == other.height_);
    std::memcpy(pixels_.data(), other.pixels_.data(), pixels_.size());
}

void FrameBuffer::Fill(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    for (std::size_t i = 0; i + 3 < pixels_.size(); i += 4) {
        pixels_[i] = r;
        pixels_[i + 1] = g;
        pixels_[i + 2] = b;
        pixels_[i + 3] = a;
    }
}

} // namespace cortex::video_editor
