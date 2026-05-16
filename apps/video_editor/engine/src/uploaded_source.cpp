#include <video_editor/uploaded_source.hpp>

#include <stdexcept>

namespace cortex::video_editor {

UploadedFrameSource::UploadedFrameSource(int width, int height, int frame_count)
    : width_(width)
    , height_(height) {
    if (width <= 0 || height <= 0 || frame_count <= 0) {
        throw std::invalid_argument("UploadedFrameSource dimensions must be positive");
    }
    frames_.reserve(static_cast<std::size_t>(frame_count));
    for (int i = 0; i < frame_count; ++i) {
        frames_.emplace_back(width, height);
    }
}

const FrameBuffer& UploadedFrameSource::At(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(frames_.size())) {
        throw std::out_of_range("UploadedFrameSource::At: index out of range");
    }
    return frames_[static_cast<std::size_t>(idx)];
}

std::uint8_t* UploadedFrameSource::WritablePixels(int idx) noexcept {
    if (idx < 0 || idx >= static_cast<int>(frames_.size())) {
        return nullptr;
    }
    return frames_[static_cast<std::size_t>(idx)].Data();
}

} // namespace cortex::video_editor
