#include <video_editor/filters/contrast.hpp>

#include <cortex/tiny_fiber/yield.hpp>

#include <algorithm>
#include <cassert>

namespace cortex::video_editor::filters {

namespace {

inline std::uint8_t ClampByte(float v) noexcept {
    return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 255.0f));
}

} // namespace

ContrastFilter::ContrastFilter(float gain) noexcept
    : gain_(gain) {}

void ContrastFilter::Apply(const FrameBuffer& in, FrameBuffer& out) const {
    assert(in.Width() == out.Width() && in.Height() == out.Height());

    const auto* src = in.Data();
    auto* dst = out.Data();
    const std::size_t width = static_cast<std::size_t>(in.Width());
    const std::size_t height = static_cast<std::size_t>(in.Height());

    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t i = y * width; i < (y + 1) * width; ++i) {
            const std::size_t off = i * 4u;
            dst[off + 0] = ClampByte((static_cast<float>(src[off + 0]) - 128.0f) * gain_ + 128.0f);
            dst[off + 1] = ClampByte((static_cast<float>(src[off + 1]) - 128.0f) * gain_ + 128.0f);
            dst[off + 2] = ClampByte((static_cast<float>(src[off + 2]) - 128.0f) * gain_ + 128.0f);
            dst[off + 3] = src[off + 3];
        }
        // Lets a fiber running this filter yield once its time slice is
        // spent; a no-op when the filter is called outside a fiber.
        tiny_fiber::CheckPoint();
    }
}

} // namespace cortex::video_editor::filters
