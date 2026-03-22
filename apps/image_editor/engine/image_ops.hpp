#pragma once

#include <cstdint>
#include <exception>
#include <vector>

namespace cortex::image_editor::mvp {

struct ImageBuffer {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;

    [[nodiscard]] bool empty() const noexcept {
        return width <= 0 || height <= 0 || pixels.empty();
    }

    [[nodiscard]] std::size_t ByteSize() const noexcept {
        return pixels.size();
    }
};

struct FilterParams {
    float grayscale_amount = 0.0F;
    int blur_radius = 0;
};

struct CancellationToken {
    bool cancelled = false;
};

class JobCancelled final : public std::exception {
public:
    [[nodiscard]] const char* what() const noexcept override {
        return "image processing job cancelled";
    }
};

class CheckpointContext {
public:
    virtual ~CheckpointContext() = default;
    virtual void Checkpoint(std::uint64_t work_units = 1) = 0;
};

void ApplyFilters(const ImageBuffer& source,
                  const FilterParams& params,
                  CheckpointContext& ctx,
                  CancellationToken& token,
                  ImageBuffer& output);

} // namespace cortex::image_editor::mvp
