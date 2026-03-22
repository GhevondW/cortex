#include "image_ops.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace cortex::image_editor::mvp {
namespace {

constexpr std::size_t kChannels = 4;
constexpr std::uint64_t kGrayscaleRowsPerCheckpoint = 24;
constexpr std::uint64_t kBlurRowsPerCheckpoint = 8;

[[nodiscard]] std::size_t PixelIndex(int x, int y, int width) {
    return static_cast<std::size_t>(y * width + x) * kChannels;
}

[[nodiscard]] int ClampCoord(int value, int min_value, int max_value) {
    return std::clamp(value, min_value, max_value);
}

[[nodiscard]] std::uint8_t ClampByte(float value) {
    const auto rounded = static_cast<int>(std::lround(value));
    return static_cast<std::uint8_t>(std::clamp(rounded, 0, 255));
}

void ThrowIfCancelled(const CancellationToken& token) {
    if (token.cancelled) {
        throw JobCancelled {};
    }
}

void CheckpointMaybe(CheckpointContext& ctx,
                     const CancellationToken& token,
                     std::uint64_t row_index,
                     std::uint64_t rows_per_checkpoint) {
    ThrowIfCancelled(token);
    if (row_index % rows_per_checkpoint == 0U) {
        ctx.Checkpoint(1);
        ThrowIfCancelled(token);
    }
}

void CopyImage(const ImageBuffer& source, ImageBuffer& output) {
    output.width = source.width;
    output.height = source.height;
    output.pixels = source.pixels;
}

void ApplyGrayscaleBlend(const ImageBuffer& source,
                         float amount,
                         CheckpointContext& ctx,
                         const CancellationToken& token,
                         ImageBuffer& output) {
    output.width = source.width;
    output.height = source.height;
    output.pixels.resize(source.pixels.size());

    const float blend = std::clamp(amount, 0.0F, 1.0F);
    const float keep = 1.0F - blend;

    for (int y = 0; y < source.height; ++y) {
        CheckpointMaybe(ctx, token, static_cast<std::uint64_t>(y), kGrayscaleRowsPerCheckpoint);

        for (int x = 0; x < source.width; ++x) {
            const auto index = PixelIndex(x, y, source.width);
            const auto r = static_cast<float>(source.pixels[index + 0U]);
            const auto g = static_cast<float>(source.pixels[index + 1U]);
            const auto b = static_cast<float>(source.pixels[index + 2U]);
            const auto a = source.pixels[index + 3U];

            const float gray = (0.299F * r) + (0.587F * g) + (0.114F * b);

            output.pixels[index + 0U] = ClampByte((keep * r) + (blend * gray));
            output.pixels[index + 1U] = ClampByte((keep * g) + (blend * gray));
            output.pixels[index + 2U] = ClampByte((keep * b) + (blend * gray));
            output.pixels[index + 3U] = a;
        }
    }
}

void ApplySeparableBoxBlur(const ImageBuffer& source,
                           int radius,
                           CheckpointContext& ctx,
                           const CancellationToken& token,
                           ImageBuffer& output) {
    if (radius <= 0) {
        CopyImage(source, output);
        return;
    }

    ImageBuffer temp;
    temp.width = source.width;
    temp.height = source.height;
    temp.pixels.resize(source.pixels.size());

    output.width = source.width;
    output.height = source.height;
    output.pixels.resize(source.pixels.size());

    const int kernel_size = (radius * 2) + 1;
    const int max_x = source.width - 1;
    const int max_y = source.height - 1;

    for (int y = 0; y < source.height; ++y) {
        CheckpointMaybe(ctx, token, static_cast<std::uint64_t>(y), kBlurRowsPerCheckpoint);

        std::array<int, kChannels> sum = {0, 0, 0, 0};
        for (int sample = -radius; sample <= radius; ++sample) {
            const auto clamped_x = ClampCoord(sample, 0, max_x);
            const auto index = PixelIndex(clamped_x, y, source.width);
            for (std::size_t channel = 0; channel < kChannels; ++channel) {
                sum[channel] += static_cast<int>(source.pixels[index + channel]);
            }
        }

        for (int x = 0; x < source.width; ++x) {
            const auto out_index = PixelIndex(x, y, source.width);
            for (std::size_t channel = 0; channel < kChannels; ++channel) {
                temp.pixels[out_index + channel] = static_cast<std::uint8_t>(sum[channel] / kernel_size);
            }

            const auto remove_x = ClampCoord(x - radius, 0, max_x);
            const auto add_x = ClampCoord(x + radius + 1, 0, max_x);
            const auto remove_index = PixelIndex(remove_x, y, source.width);
            const auto add_index = PixelIndex(add_x, y, source.width);
            for (std::size_t channel = 0; channel < kChannels; ++channel) {
                sum[channel] += static_cast<int>(source.pixels[add_index + channel]) -
                                static_cast<int>(source.pixels[remove_index + channel]);
            }
        }
    }

    for (int x = 0; x < temp.width; ++x) {
        std::array<int, kChannels> sum = {0, 0, 0, 0};
        for (int sample = -radius; sample <= radius; ++sample) {
            const auto clamped_y = ClampCoord(sample, 0, max_y);
            const auto index = PixelIndex(x, clamped_y, temp.width);
            for (std::size_t channel = 0; channel < kChannels; ++channel) {
                sum[channel] += static_cast<int>(temp.pixels[index + channel]);
            }
        }

        for (int y = 0; y < temp.height; ++y) {
            if (x == 0) {
                CheckpointMaybe(ctx, token, static_cast<std::uint64_t>(y), kBlurRowsPerCheckpoint);
            }

            const auto out_index = PixelIndex(x, y, temp.width);
            for (std::size_t channel = 0; channel < kChannels; ++channel) {
                output.pixels[out_index + channel] = static_cast<std::uint8_t>(sum[channel] / kernel_size);
            }

            const auto remove_y = ClampCoord(y - radius, 0, max_y);
            const auto add_y = ClampCoord(y + radius + 1, 0, max_y);
            const auto remove_index = PixelIndex(x, remove_y, temp.width);
            const auto add_index = PixelIndex(x, add_y, temp.width);
            for (std::size_t channel = 0; channel < kChannels; ++channel) {
                sum[channel] += static_cast<int>(temp.pixels[add_index + channel]) -
                                static_cast<int>(temp.pixels[remove_index + channel]);
            }
        }
    }
}

} // namespace

void ApplyFilters(const ImageBuffer& source,
                  const FilterParams& params,
                  CheckpointContext& ctx,
                  CancellationToken& token,
                  ImageBuffer& output) {
    if (source.empty()) {
        throw std::invalid_argument("source image is empty");
    }

    if ((static_cast<std::size_t>(source.width) * static_cast<std::size_t>(source.height) * kChannels) !=
        source.pixels.size()) {
        throw std::invalid_argument("source image buffer size does not match dimensions");
    }

    ThrowIfCancelled(token);

    const float grayscale_amount = std::clamp(params.grayscale_amount, 0.0F, 1.0F);
    const int blur_radius = std::max(params.blur_radius, 0);

    if (grayscale_amount <= 0.0F && blur_radius <= 0) {
        CopyImage(source, output);
        return;
    }

    if (blur_radius <= 0) {
        ApplyGrayscaleBlend(source, grayscale_amount, ctx, token, output);
        return;
    }

    if (grayscale_amount <= 0.0F) {
        ApplySeparableBoxBlur(source, blur_radius, ctx, token, output);
        return;
    }

    ImageBuffer grayscale_stage;
    ApplyGrayscaleBlend(source, grayscale_amount, ctx, token, grayscale_stage);
    ApplySeparableBoxBlur(grayscale_stage, blur_radius, ctx, token, output);
}

} // namespace cortex::image_editor::mvp
