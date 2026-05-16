#pragma once

#include <video_editor/filter.hpp>

#include <vector>

namespace cortex::video_editor::filters {

// Separable Gaussian blur. `radius` in pixels: 0 is identity, larger values
// are progressively heavier. Kernel sigma derives from radius (sigma = max(0.5,
// radius/2)) and the kernel is computed once in the constructor.
//
// Apply() does two passes through a scratch buffer (horizontal then vertical).
// The scratch buffer is held as a mutable member so chained Apply() calls
// don't reallocate; this is the heavy filter that the demo showcases.
class GaussianBlurFilter final : public IFilter {
public:
    explicit GaussianBlurFilter(int radius);

    void Apply(const FrameBuffer& in, FrameBuffer& out) const override;

private:
    int radius_;
    std::vector<float> kernel_;
    mutable FrameBuffer scratch_;
};

} // namespace cortex::video_editor::filters
