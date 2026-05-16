#include <video_editor/procedural_source.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace cortex::video_editor {

namespace {

inline std::uint8_t ClampByte(float v) noexcept {
    return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 255.0f));
}

struct Disc {
    float cx; // center x in pixels
    float cy; // center y in pixels
    float r; // radius in pixels
    std::uint8_t cr;
    std::uint8_t cg;
    std::uint8_t cb;
};

// Returns three orbiting discs for the given frame index. Centers move along
// Lissajous figures so the motion is smooth and infinite.
std::array<Disc, 3> ComputeDiscs(int frame_idx, int width, int height) {
    const float t = static_cast<float>(frame_idx) * 0.04f;
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float r = std::min(w, h) * 0.12f;

    std::array<Disc, 3> discs {{
        {0.50f * w + 0.30f * w * std::sin(t * 1.1f + 0.0f),
         0.50f * h + 0.25f * h * std::cos(t * 1.3f + 1.0f),
         r,
         235,
         88,
         96},
        {0.50f * w + 0.28f * w * std::sin(t * 0.7f + 2.0f),
         0.50f * h + 0.30f * h * std::cos(t * 0.9f + 3.0f),
         r * 0.85f,
         96,
         200,
         235},
        {0.50f * w + 0.32f * w * std::cos(t * 1.5f + 4.5f),
         0.50f * h + 0.22f * h * std::sin(t * 1.8f + 5.5f),
         r * 1.1f,
         235,
         210,
         96},
    }};
    return discs;
}

} // namespace

ProceduralFrameSource::ProceduralFrameSource(int width, int height, int frame_count)
    : width_(width)
    , height_(height) {
    if (width <= 0 || height <= 0 || frame_count <= 0) {
        throw std::invalid_argument("ProceduralFrameSource dimensions must be positive");
    }
    frames_.reserve(static_cast<std::size_t>(frame_count));
    for (int i = 0; i < frame_count; ++i) {
        FrameBuffer fb(width, height);
        RenderFrame(i, fb);
        frames_.push_back(std::move(fb));
    }
}

const FrameBuffer& ProceduralFrameSource::At(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(frames_.size())) {
        throw std::out_of_range("ProceduralFrameSource::At: index out of range");
    }
    return frames_[static_cast<std::size_t>(idx)];
}

void ProceduralFrameSource::RenderFrame(int idx, FrameBuffer& out) const {
    const float t = static_cast<float>(idx) * 0.05f;
    const auto discs = ComputeDiscs(idx, width_, height_);
    auto* dst = out.Data();

    for (int y = 0; y < height_; ++y) {
        const float fy = static_cast<float>(y) / static_cast<float>(height_);
        for (int x = 0; x < width_; ++x) {
            const float fx = static_cast<float>(x) / static_cast<float>(width_);

            // Background gradient: layered sinusoids in three channels.
            const float br = 0.5f + 0.5f * std::sin((fx + t * 0.3f) * 4.0f);
            const float bg = 0.5f + 0.5f * std::sin((fy * 3.0f + t * 0.5f) * 1.7f);
            const float bb = 0.5f + 0.5f * std::sin(((fx + fy) * 2.5f + t * 0.7f));

            float r = br * 220.0f + 30.0f;
            float g = bg * 200.0f + 35.0f;
            float b = bb * 230.0f + 25.0f;

            // Composite anti-aliased discs over the gradient.
            for (const auto& d : discs) {
                const float dx = static_cast<float>(x) - d.cx;
                const float dy = static_cast<float>(y) - d.cy;
                const float dist = std::sqrt(dx * dx + dy * dy);
                const float edge = d.r;
                const float aa = 1.5f; // anti-alias width in px
                const float alpha = std::clamp((edge - dist) / aa, 0.0f, 1.0f);
                if (alpha > 0.0f) {
                    r = r * (1.0f - alpha) + static_cast<float>(d.cr) * alpha;
                    g = g * (1.0f - alpha) + static_cast<float>(d.cg) * alpha;
                    b = b * (1.0f - alpha) + static_cast<float>(d.cb) * alpha;
                }
            }

            const std::size_t off =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)) * 4u;
            dst[off + 0] = ClampByte(r);
            dst[off + 1] = ClampByte(g);
            dst[off + 2] = ClampByte(b);
            dst[off + 3] = 255;
        }
    }
}

} // namespace cortex::video_editor
