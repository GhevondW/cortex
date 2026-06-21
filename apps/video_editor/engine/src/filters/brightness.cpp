#include <video_editor/filters/brightness.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>

namespace cortex::video_editor::filters {

namespace {

inline std::uint8_t ClampByte(float v) noexcept {
    return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 255.0f));
}

} // namespace

BrightnessFilter::BrightnessFilter(float delta) noexcept
    : delta_(delta) {}

void BrightnessFilter::Apply(const FrameBuffer& in, FrameBuffer& out) const {
    assert(in.Width() == out.Width() && in.Height() == out.Height());

    const float shift = delta_ * 255.0f;
    const auto* src = in.Data();
    auto* dst = out.Data();
    const std::size_t pixel_count = static_cast<std::size_t>(in.Width()) * static_cast<std::size_t>(in.Height());

    for (std::size_t i = 0; i < pixel_count; ++i) {
        const std::size_t off = i * 4u;
        dst[off + 0] = ClampByte(static_cast<float>(src[off + 0]) + shift);
        dst[off + 1] = ClampByte(static_cast<float>(src[off + 1]) + shift);
        dst[off + 2] = ClampByte(static_cast<float>(src[off + 2]) + shift);
        dst[off + 3] = src[off + 3];
    }
}

} // namespace cortex::video_editor::filters
