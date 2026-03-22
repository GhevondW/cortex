#include "editor_session.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace tf = cortex::tiny_fiber;

namespace cortex::image_editor::mvp {
namespace {

constexpr int kBytesPerPixel = 4;
constexpr std::uint64_t kCheckpointBudgetUnits = 4;

[[nodiscard]] ImageBuffer CopyIncomingImage(const std::uint8_t* pixels, int width, int height, int stride_bytes) {
    if (pixels == nullptr) {
        throw std::invalid_argument("image pointer is null");
    }
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("image dimensions must be positive");
    }
    if (stride_bytes < width * kBytesPerPixel) {
        throw std::invalid_argument("image stride is smaller than width * 4");
    }

    ImageBuffer image;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<std::size_t>(width * height * kBytesPerPixel));

    for (int y = 0; y < height; ++y) {
        const auto* src_row = pixels + (static_cast<std::size_t>(y) * static_cast<std::size_t>(stride_bytes));
        auto* dst_row =
            image.pixels.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width * kBytesPerPixel));
        std::memcpy(dst_row, src_row, static_cast<std::size_t>(width * kBytesPerPixel));
    }

    return image;
}

} // namespace

class EditorSession::FiberCheckpointContext final : public CheckpointContext {
public:
    explicit FiberCheckpointContext(CancellationToken& token)
        : token_(token) {}

    void Checkpoint(std::uint64_t work_units) override {
        if (token_.cancelled) {
            throw JobCancelled {};
        }

        budget_ += std::max<std::uint64_t>(1, work_units);
        if (budget_ >= kCheckpointBudgetUnits) {
            budget_ = 0;
            tf::Yield();
            if (token_.cancelled) {
                throw JobCancelled {};
            }
        }
    }

private:
    CancellationToken& token_;
    std::uint64_t budget_ = 0;
};

EditorSession::EditorSession() {
    requested_params_ = current_params_;
}

void EditorSession::Initialize() {
    if (scheduler_) {
        return;
    }

    scheduler_ = tf::Scheduler::Create([this] {
        ControlLoop();
    });
}

void EditorSession::SetSourceImage(const std::uint8_t* pixels, int width, int height, int stride_bytes) {
    Initialize();

    try {
        staged_source_image_ = CopyIncomingImage(pixels, width, height, stride_bytes);
        error_message_.clear();
    } catch (const std::exception& ex) {
        SetError(ex.what());
    }
}

void EditorSession::SetGrayscaleAmount(float value) {
    Initialize();
    requested_params_.grayscale_amount = std::clamp(value, 0.0F, 1.0F);
    has_pending_params_update_ = true;
}

void EditorSession::SetBlurRadius(int value) {
    Initialize();
    requested_params_.blur_radius = std::clamp(value, 0, 32);
    has_pending_params_update_ = true;
}

int EditorSession::Pump(int max_steps) {
    Initialize();

    if (max_steps <= 0) {
        return NeedsPump() ? 1 : 0;
    }

    int steps = 0;
    while (steps < max_steps && NeedsPump()) {
        scheduler_->Step();
        ++steps;
    }

    return NeedsPump() ? 1 : 0;
}

bool EditorSession::NeedsPump() const noexcept {
    return staged_source_image_.has_value() || has_pending_params_update_ || active_job_.has_value() ||
           (HasSourceImage() && desired_revision_ > rendered_revision_);
}

EditorSession::Status EditorSession::GetStatus() const noexcept {
    return status_;
}

int EditorSession::GetOutputWidth() const noexcept {
    return output_image_.width;
}

int EditorSession::GetOutputHeight() const noexcept {
    return output_image_.height;
}

const std::uint8_t* EditorSession::GetOutputPixels() const noexcept {
    return output_image_.pixels.empty() ? nullptr : output_image_.pixels.data();
}

int EditorSession::GetOutputSize() const noexcept {
    return static_cast<int>(output_image_.pixels.size());
}

std::uint32_t EditorSession::GetOutputRevision() const noexcept {
    return output_revision_;
}

const char* EditorSession::GetErrorMessage() const noexcept {
    return error_message_.c_str();
}

int EditorSession::CopyOutputPixels(std::uint8_t* destination, int capacity) const noexcept {
    if (destination == nullptr || capacity < 0) {
        return 0;
    }

    const auto size = static_cast<int>(output_image_.pixels.size());
    if (size <= 0 || capacity < size) {
        return 0;
    }

    std::memcpy(destination, output_image_.pixels.data(), static_cast<std::size_t>(size));
    return size;
}

void EditorSession::ControlLoop() {
    while (!tf::IsStopping()) {
        ApplyPendingHostChanges();
        ReapCompletedJob();
        MaybeStartJob();
        tf::Yield();
    }
}

void EditorSession::ApplyPendingHostChanges() {
    bool changed = false;
    bool source_changed = false;

    if (staged_source_image_) {
        source_image_ = std::move(*staged_source_image_);
        staged_source_image_.reset();
        output_image_ = {};
        error_message_.clear();
        changed = true;
        source_changed = true;
    }

    if (has_pending_params_update_) {
        current_params_ = requested_params_;
        has_pending_params_update_ = false;
        changed = true;
    }

    if (!changed) {
        return;
    }

    if (!HasSourceImage()) {
        status_ = Status::kNoImage;
        return;
    }

    MarkDirty(source_changed);
}

void EditorSession::ReapCompletedJob() {
    if (!active_job_ || !active_job_->future.has_value() || !active_job_->future->IsReady()) {
        return;
    }

    try {
        active_job_->future->Wait();
    } catch (const std::exception& ex) {
        if (active_job_->result->error_message.empty()) {
            active_job_->result->error_message = ex.what();
        }
    }

    const auto result = active_job_->result;
    const auto completed_revision = active_job_->revision;
    const bool is_latest_job = completed_revision == desired_revision_;

    if (result->success) {
        output_image_ = std::move(result->output);
        rendered_revision_ = completed_revision;
        ++output_revision_;
        error_message_.clear();
        status_ = is_latest_job ? Status::kReady : Status::kProcessing;
    } else if (!result->error_message.empty() && is_latest_job) {
        rendered_revision_ = completed_revision;
        SetError(result->error_message);
    }

    active_job_.reset();

    if (HasSourceImage() && desired_revision_ > rendered_revision_) {
        status_ = Status::kProcessing;
    }
}

void EditorSession::MaybeStartJob() {
    if (active_job_ || !HasSourceImage() || desired_revision_ <= rendered_revision_) {
        return;
    }

    auto source_snapshot = source_image_;
    const auto params_snapshot = current_params_;

    ActiveJob job;
    job.revision = desired_revision_;
    job.token = std::make_shared<CancellationToken>();
    job.result = std::make_shared<JobResult>();

    auto token = job.token;
    auto result = job.result;

    status_ = Status::kProcessing;
    job.future.emplace(tf::Spawn([source = std::move(source_snapshot), params_snapshot, token, result]() mutable {
        try {
            FiberCheckpointContext ctx(*token);
            ApplyFilters(source, params_snapshot, ctx, *token, result->output);
            result->cancelled = token->cancelled;
            result->success = !result->cancelled;
        } catch (const JobCancelled&) {
            result->cancelled = true;
        } catch (const std::exception& ex) {
            result->error_message = ex.what();
        }
    }));

    active_job_ = std::move(job);
}

void EditorSession::MarkDirty(bool cancel_active_job) {
    ++desired_revision_;
    if (cancel_active_job && active_job_) {
        active_job_->token->cancelled = true;
    }
    status_ = Status::kProcessing;
}

void EditorSession::SetError(std::string message) {
    error_message_ = std::move(message);
    status_ = Status::kError;
}

bool EditorSession::HasSourceImage() const noexcept {
    return !source_image_.empty();
}

} // namespace cortex::image_editor::mvp
