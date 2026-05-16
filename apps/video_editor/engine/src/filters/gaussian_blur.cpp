#include <video_editor/filters/gaussian_blur.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>

namespace cortex::video_editor::filters {

namespace {

inline std::uint8_t ClampByte(float v) noexcept {
    return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 255.0f));
}

std::vector<float> BuildKernel(int radius) {
    if (radius <= 0) {
        return {1.0f};
    }
    const float sigma = std::max(0.5f, static_cast<float>(radius) / 2.0f);
    const float two_sigma2 = 2.0f * sigma * sigma;

    std::vector<float> k(static_cast<std::size_t>(2 * radius + 1));
    float sum = 0.0f;
    for (int i = -radius; i <= radius; ++i) {
        const float w = std::exp(-static_cast<float>(i * i) / two_sigma2);
        k[static_cast<std::size_t>(i + radius)] = w;
        sum += w;
    }
    for (auto& v : k) {
        v /= sum;
    }
    return k;
}

void BlurHorizontal(const FrameBuffer& in, FrameBuffer& out, const std::vector<float>& kernel, int radius) {
    const int w = in.Width();
    const int h = in.Height();
    const auto* src = in.Data();
    auto* dst = out.Data();

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float r = 0.0f, g = 0.0f, b = 0.0f;
            for (int k = -radius; k <= radius; ++k) {
                const int sx = std::clamp(x + k, 0, w - 1);
                const std::size_t off =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(sx)) * 4u;
                const float weight = kernel[static_cast<std::size_t>(k + radius)];
                r += weight * static_cast<float>(src[off + 0]);
                g += weight * static_cast<float>(src[off + 1]);
                b += weight * static_cast<float>(src[off + 2]);
            }
            const std::size_t dst_off =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4u;
            dst[dst_off + 0] = ClampByte(r);
            dst[dst_off + 1] = ClampByte(g);
            dst[dst_off + 2] = ClampByte(b);
            dst[dst_off + 3] = src[dst_off + 3];
        }
    }
}

void BlurVertical(const FrameBuffer& in, FrameBuffer& out, const std::vector<float>& kernel, int radius) {
    const int w = in.Width();
    const int h = in.Height();
    const auto* src = in.Data();
    auto* dst = out.Data();

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float r = 0.0f, g = 0.0f, b = 0.0f;
            for (int k = -radius; k <= radius; ++k) {
                const int sy = std::clamp(y + k, 0, h - 1);
                const std::size_t off =
                    (static_cast<std::size_t>(sy) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4u;
                const float weight = kernel[static_cast<std::size_t>(k + radius)];
                r += weight * static_cast<float>(src[off + 0]);
                g += weight * static_cast<float>(src[off + 1]);
                b += weight * static_cast<float>(src[off + 2]);
            }
            const std::size_t dst_off =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4u;
            dst[dst_off + 0] = ClampByte(r);
            dst[dst_off + 1] = ClampByte(g);
            dst[dst_off + 2] = ClampByte(b);
            dst[dst_off + 3] = src[dst_off + 3];
        }
    }
}

} // namespace

GaussianBlurFilter::GaussianBlurFilter(int radius)
    : radius_(radius < 0 ? 0 : radius)
    , kernel_(BuildKernel(radius_)) {}

void GaussianBlurFilter::Apply(const FrameBuffer& in, FrameBuffer& out) const {
    assert(in.Width() == out.Width() && in.Height() == out.Height());

    if (radius_ == 0) {
        out.CopyFrom(in);
        return;
    }

    if (scratch_.Width() != in.Width() || scratch_.Height() != in.Height()) {
        scratch_ = FrameBuffer(in.Width(), in.Height());
    }

    BlurHorizontal(in, scratch_, kernel_, radius_);
    BlurVertical(scratch_, out, kernel_, radius_);
}

} // namespace cortex::video_editor::filters
