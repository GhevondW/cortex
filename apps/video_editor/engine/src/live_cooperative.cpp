#include <video_editor/live_cooperative.hpp>

#include <cortex/tiny_fiber/yield.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cortex::video_editor {

namespace tf = cortex::tiny_fiber;

namespace {

// All math below is duplicated verbatim from the synchronous filters
// (filters/*.cpp) so the cooperative output is byte-identical. The only
// difference is that the row loop is split into bands with a tf::Yield() between
// them. The live_cooperative_test enforces the byte-for-byte equivalence, so any
// drift from the filters is caught at build time.

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

// Runs `op(src_pixel, dst_pixel)` over every pixel, yielding after each band of
// `band_rows` rows. Each output pixel depends only on the same input pixel, so
// banding is exact regardless of band size.
template <typename Op>
void BandedPerPixel(const FrameBuffer& in, FrameBuffer& out, int band_rows, Op op) {
    const int w = in.Width();
    const int h = in.Height();
    const auto* src = in.Data();
    auto* dst = out.Data();
    for (int y0 = 0; y0 < h; y0 += band_rows) {
        const int y1 = std::min(h, y0 + band_rows);
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < w; ++x) {
                const std::size_t off =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4u;
                op(src + off, dst + off);
            }
        }
        tf::Yield();
    }
}

// Horizontal blur pass, banded by output rows. Each output row reads only its
// own input row, so banding is exact.
void BlurHorizontalBanded(
    const FrameBuffer& in, FrameBuffer& out, const std::vector<float>& kernel, int radius, int band_rows) {
    const int w = in.Width();
    const int h = in.Height();
    const auto* src = in.Data();
    auto* dst = out.Data();

    for (int y0 = 0; y0 < h; y0 += band_rows) {
        const int y1 = std::min(h, y0 + band_rows);
        for (int y = y0; y < y1; ++y) {
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
        tf::Yield();
    }
}

// Vertical blur pass, banded by output rows. Reads the fully-materialized
// horizontal-pass buffer, so banding output rows is exact.
void BlurVerticalBanded(
    const FrameBuffer& in, FrameBuffer& out, const std::vector<float>& kernel, int radius, int band_rows) {
    const int w = in.Width();
    const int h = in.Height();
    const auto* src = in.Data();
    auto* dst = out.Data();

    for (int y0 = 0; y0 < h; y0 += band_rows) {
        const int y1 = std::min(h, y0 + band_rows);
        for (int y = y0; y < y1; ++y) {
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
        tf::Yield();
    }
}

} // namespace

struct LiveCooperativeRenderer::State {
    int band_rows;
    LiveFilterParams params;
    FrameBuffer work_a;
    FrameBuffer work_b;
    FrameBuffer scratch;
    FrameBuffer output;
};

LiveCooperativeRenderer::LiveCooperativeRenderer(int band_rows)
    : band_rows_(std::max(1, band_rows)) {}

LiveCooperativeRenderer::~LiveCooperativeRenderer() = default;

void LiveCooperativeRenderer::Begin(const FrameBuffer& source, const LiveFilterParams& params) {
    // Drop any in-flight render (tears the old scheduler down cleanly).
    scheduler_.reset();

    auto state = std::make_shared<State>();
    state->band_rows = band_rows_;
    state->params = params;
    state->work_a = source; // copy of the source frame
    state->work_b = FrameBuffer(source.Width(), source.Height());
    state->scratch = FrameBuffer(source.Width(), source.Height());
    state->output = FrameBuffer(source.Width(), source.Height());
    state_ = state;
    done_ = false;

    scheduler_ = tf::Scheduler::Create([state] {
        State& s = *state;
        FrameBuffer* cur = &s.work_a;
        FrameBuffer* other = &s.work_b;

        if (s.params.brightness != 0.0f) {
            const float shift = s.params.brightness * 255.0f;
            BandedPerPixel(*cur, *other, s.band_rows, [shift](const std::uint8_t* in, std::uint8_t* out) {
                out[0] = ClampByte(static_cast<float>(in[0]) + shift);
                out[1] = ClampByte(static_cast<float>(in[1]) + shift);
                out[2] = ClampByte(static_cast<float>(in[2]) + shift);
                out[3] = in[3];
            });
            std::swap(cur, other);
        }
        if (s.params.contrast != 1.0f) {
            const float g = s.params.contrast;
            BandedPerPixel(*cur, *other, s.band_rows, [g](const std::uint8_t* in, std::uint8_t* out) {
                out[0] = ClampByte((static_cast<float>(in[0]) - 128.0f) * g + 128.0f);
                out[1] = ClampByte((static_cast<float>(in[1]) - 128.0f) * g + 128.0f);
                out[2] = ClampByte((static_cast<float>(in[2]) - 128.0f) * g + 128.0f);
                out[3] = in[3];
            });
            std::swap(cur, other);
        }
        if (s.params.saturation != 1.0f) {
            const float scale = s.params.saturation;
            BandedPerPixel(*cur, *other, s.band_rows, [scale](const std::uint8_t* in, std::uint8_t* out) {
                const float r = static_cast<float>(in[0]);
                const float g = static_cast<float>(in[1]);
                const float b = static_cast<float>(in[2]);
                const float luma = 0.299f * r + 0.587f * g + 0.114f * b;
                out[0] = ClampByte(luma + (r - luma) * scale);
                out[1] = ClampByte(luma + (g - luma) * scale);
                out[2] = ClampByte(luma + (b - luma) * scale);
                out[3] = in[3];
            });
            std::swap(cur, other);
        }
        if (s.params.blur_radius > 0) {
            const auto kernel = BuildKernel(s.params.blur_radius);
            BlurHorizontalBanded(*cur, s.scratch, kernel, s.params.blur_radius, s.band_rows);
            BlurVerticalBanded(s.scratch, *other, kernel, s.params.blur_radius, s.band_rows);
            std::swap(cur, other);
        }

        s.output.CopyFrom(*cur);
    });
}

bool LiveCooperativeRenderer::Step() {
    if (!scheduler_ || done_) {
        return false;
    }
    const bool more = scheduler_->Step();
    if (!more) {
        done_ = true;
        scheduler_.reset();
    }
    return more;
}

const FrameBuffer& LiveCooperativeRenderer::Output() const noexcept {
    return state_->output;
}

} // namespace cortex::video_editor
