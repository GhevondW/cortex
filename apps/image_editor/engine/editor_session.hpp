#pragma once

#include "image_ops.hpp"

#include <cortex/tiny_fiber/tiny_fiber.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace cortex::image_editor::mvp {

class EditorSession {
public:
    enum class Status : int {
        kNoImage = 0,
        kIdle = 1,
        kProcessing = 2,
        kReady = 3,
        kError = 4,
    };

    EditorSession();
    ~EditorSession() = default;

    EditorSession(const EditorSession&) = delete;
    EditorSession& operator=(const EditorSession&) = delete;

    void Initialize();
    void SetSourceImage(const std::uint8_t* pixels, int width, int height, int stride_bytes);
    void SetGrayscaleAmount(float value);
    void SetBlurRadius(int value);

    [[nodiscard]] int Pump(int max_steps);
    [[nodiscard]] bool NeedsPump() const noexcept;

    [[nodiscard]] Status GetStatus() const noexcept;
    [[nodiscard]] int GetOutputWidth() const noexcept;
    [[nodiscard]] int GetOutputHeight() const noexcept;
    [[nodiscard]] const std::uint8_t* GetOutputPixels() const noexcept;
    [[nodiscard]] int GetOutputSize() const noexcept;
    [[nodiscard]] std::uint32_t GetOutputRevision() const noexcept;
    [[nodiscard]] const char* GetErrorMessage() const noexcept;
    [[nodiscard]] int CopyOutputPixels(std::uint8_t* destination, int capacity) const noexcept;

private:
    struct JobResult {
        bool success = false;
        bool cancelled = false;
        std::string error_message;
        ImageBuffer output;
    };

    struct ActiveJob {
        std::uint64_t revision = 0;
        std::shared_ptr<CancellationToken> token;
        std::shared_ptr<JobResult> result;
        std::optional<cortex::tiny_fiber::Future<void>> future;
    };

    class FiberCheckpointContext;

    void ControlLoop();
    void ApplyPendingHostChanges();
    void ReapCompletedJob();
    void MaybeStartJob();
    void MarkDirty(bool cancel_active_job);
    void SetError(std::string message);
    [[nodiscard]] bool HasSourceImage() const noexcept;

private:
    std::unique_ptr<cortex::tiny_fiber::Scheduler> scheduler_;

    FilterParams current_params_ {};
    FilterParams requested_params_ {};
    bool has_pending_params_update_ = false;

    std::optional<ImageBuffer> staged_source_image_;

    ImageBuffer source_image_;
    ImageBuffer output_image_;

    std::optional<ActiveJob> active_job_;

    std::uint64_t desired_revision_ = 0;
    std::uint64_t rendered_revision_ = 0;
    std::uint32_t output_revision_ = 0;

    Status status_ = Status::kNoImage;
    std::string error_message_;
};

} // namespace cortex::image_editor::mvp
