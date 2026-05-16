#pragma once

#include <video_editor/frame_buffer.hpp>

namespace cortex::video_editor {

// Strategy interface for a single image-processing operation. Implementations
// read `in` and write `out`. Both buffers have identical dimensions, sized
// before Apply() is called.
class IFilter {
public:
    virtual ~IFilter() = default;

    virtual void Apply(const FrameBuffer& in, FrameBuffer& out) const = 0;
};

} // namespace cortex::video_editor
