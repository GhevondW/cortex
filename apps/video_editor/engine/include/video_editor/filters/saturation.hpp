#pragma once

#include <video_editor/filter.hpp>

namespace cortex::video_editor::filters {

// Saturation adjustment around luma. `scale` in [0.0, 2.0]; 1.0 is identity,
// 0.0 produces grayscale, 2.0 oversaturates. Operates on each pixel via the
// formula:  out = luma + (in - luma) * scale, where luma = 0.299R + 0.587G + 0.114B.
class SaturationFilter final : public IFilter {
public:
    explicit SaturationFilter(float scale) noexcept;

    void Apply(const FrameBuffer& in, FrameBuffer& out) const override;

private:
    float scale_;
};

} // namespace cortex::video_editor::filters
