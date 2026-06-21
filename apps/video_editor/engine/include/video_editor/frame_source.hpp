#pragma once

#include <video_editor/frame_buffer.hpp>

namespace cortex::video_editor {

// Strategy interface for "where do frames come from?". The procedural source
// is the only implementation today; a real-video source would slot in here
// without touching anything else.
class IFrameSource {
public:
    virtual ~IFrameSource() = default;

    [[nodiscard]] virtual int Width() const noexcept = 0;
    [[nodiscard]] virtual int Height() const noexcept = 0;
    [[nodiscard]] virtual int FrameCount() const noexcept = 0;
    [[nodiscard]] virtual const FrameBuffer& At(int idx) const = 0;
};

} // namespace cortex::video_editor
