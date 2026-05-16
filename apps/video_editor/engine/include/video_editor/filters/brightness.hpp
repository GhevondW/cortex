#pragma once

#include <video_editor/filter.hpp>

namespace cortex::video_editor::filters {

// Per-pixel linear brightness shift. `delta` in [-1.0, 1.0] is added to each
// channel (after scaling to [0,1]); 0.0 is identity.
class BrightnessFilter final : public IFilter {
public:
    explicit BrightnessFilter(float delta) noexcept;

    void Apply(const FrameBuffer& in, FrameBuffer& out) const override;

private:
    float delta_;
};

} // namespace cortex::video_editor::filters
