#include <video_editor/filters/saturation.hpp>

#include <algorithm>
#include <cassert>

namespace cortex::video_editor::filters {

namespace {

inline std::uint8_t ClampByte(float v) noexcept {
    return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 255.0f));
}

} // namespace

SaturationFilter::SaturationFilter(float scale) noexcept
    : scale_(scale) {}

void SaturationFilter::Apply(const FrameBuffer& in, FrameBuffer& out) const {
    assert(in.Width() == out.Width() && in.Height() == out.Height());

    const auto* src = in.Data();
    auto* dst = out.Data();
    const std::size_t pixel_count = static_cast<std::size_t>(in.Width()) * static_cast<std::size_t>(in.Height());

    for (std::size_t i = 0; i < pixel_count; ++i) {
        const std::size_t off = i * 4u;
        const float r = static_cast<float>(src[off + 0]);
        const float g = static_cast<float>(src[off + 1]);
        const float b = static_cast<float>(src[off + 2]);
        const float luma = 0.299f * r + 0.587f * g + 0.114f * b;

        dst[off + 0] = ClampByte(luma + (r - luma) * scale_);
        dst[off + 1] = ClampByte(luma + (g - luma) * scale_);
        dst[off + 2] = ClampByte(luma + (b - luma) * scale_);
        dst[off + 3] = src[off + 3];
    }
}

} // namespace cortex::video_editor::filters
