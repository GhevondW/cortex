#pragma once

#include <video_editor/filter.hpp>

namespace cortex::video_editor::filters {

// Per-pixel contrast adjustment around mid-gray (128). `gain` in [0.0, 2.0];
// 1.0 is identity, 0.0 flattens to mid-gray, 2.0 doubles contrast.
class ContrastFilter final : public IFilter {
public:
    explicit ContrastFilter(float gain) noexcept;

    void Apply(const FrameBuffer& in, FrameBuffer& out) const override;

private:
    float gain_;
};

} // namespace cortex::video_editor::filters
