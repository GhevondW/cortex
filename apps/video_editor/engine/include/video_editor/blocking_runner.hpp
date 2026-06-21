#pragma once

#include <video_editor/progress_listener.hpp>
#include <video_editor/runner.hpp>

namespace cortex::video_editor {

class Pipeline;

// Sequential runner — no cooperation, no fibers. Runs the entire apply-all
// loop in a single synchronous call to Start(). The JS event loop never gets
// a chance during this time, so the UI freezes. This is the negative-case
// foil for the cooperative demo.
class BlockingRunner final : public IRunner {
public:
    void Start(Pipeline& pipeline, IProgressListener& listener) override;
    bool Step() override; // no-op for blocking runner; returns false
    void Cancel() override;

    [[nodiscard]] bool IsRunning() const noexcept override {
        return false;
    }
    [[nodiscard]] float Progress() const noexcept override {
        return progress_;
    }

private:
    float progress_ {0.0f};
    bool cancel_requested_ {false};
};

} // namespace cortex::video_editor
