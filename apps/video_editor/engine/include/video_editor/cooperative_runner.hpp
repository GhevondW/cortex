#pragma once

#include <video_editor/progress_listener.hpp>
#include <video_editor/runner.hpp>

#include <cortex/tiny_fiber/scheduler.hpp>

#include <atomic>
#include <memory>

namespace cortex::video_editor {

class Pipeline;

// Cooperative runner backed by tiny_fiber. Spawns N worker fibers; each owns
// a slice of the frame index range and calls tf::Yield() after every frame so
// JS gets a turn between work units. The runner is driven by repeated Step()
// calls from JS (typically once per requestAnimationFrame).
//
// On Cancel(): scheduler->Stop() signals all suspended fibers. They observe
// IsStopping() at the next yield and throw SchedulerStoppingError, which the
// worker bodies catch and let propagate. The next Step() drains the
// remaining fibers and dispatches OnCancelled() on the listener.
class CooperativeRunner final : public IRunner {
public:
    // workers — number of parallel fibers. 1 means strictly serial-with-yields;
    // 3–4 is a good default. Workers > FrameCount() is silently clamped.
    explicit CooperativeRunner(int workers = 3);
    ~CooperativeRunner() override;

    void Start(Pipeline& pipeline, IProgressListener& listener) override;
    bool Step() override;
    void Cancel() override;

    [[nodiscard]] bool IsRunning() const noexcept override {
        return scheduler_ != nullptr && !finished_;
    }
    [[nodiscard]] float Progress() const noexcept override;

private:
    void DispatchCompletionIfDone();

    int worker_count_;
    std::unique_ptr<tiny_fiber::Scheduler> scheduler_;

    // Shared state read by workers; pipeline_ and listener_ are caller-owned.
    Pipeline* pipeline_ {nullptr};
    IProgressListener* listener_ {nullptr};

    // Frame index counter shared across workers — atomic in shape only; in
    // single-threaded cooperative scheduling there's no real contention, but
    // using atomic<int> makes the model explicit and lets future native multi-
    // thread experiments share the code.
    std::atomic<int> next_frame_ {0};
    std::atomic<int> completed_frames_ {0};

    int total_frames_ {0};
    bool finished_ {false};
    bool cancelled_ {false};
};

} // namespace cortex::video_editor
