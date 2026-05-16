#include <video_editor/cooperative_runner.hpp>

#include <video_editor/pipeline.hpp>

#include <cortex/tiny_fiber/errors/scheduler_stopping_error.hpp>
#include <cortex/tiny_fiber/future.hpp>
#include <cortex/tiny_fiber/yield.hpp>

#include <algorithm>

namespace cortex::video_editor {

namespace tf = cortex::tiny_fiber;

CooperativeRunner::CooperativeRunner(int workers)
    : worker_count_(std::max(1, workers)) {}

CooperativeRunner::~CooperativeRunner() = default;

void CooperativeRunner::Start(Pipeline& pipeline, IProgressListener& listener) {
    pipeline_ = &pipeline;
    listener_ = &listener;
    next_frame_ = 0;
    completed_frames_ = 0;
    total_frames_ = pipeline.FrameCount();
    finished_ = false;
    cancelled_ = false;

    if (total_frames_ <= 0) {
        finished_ = true;
        listener.OnComplete();
        return;
    }

    const int workers = std::min(worker_count_, total_frames_);

    // The entry fiber spawns workers, waits for them all, then returns.
    // Workers grab indices from a shared atomic counter (work-stealing style)
    // so faster workers can pick up slack from slower ones.
    scheduler_ = tf::Scheduler::Create([this, workers] {
        std::vector<tf::Future<void>> handles;
        handles.reserve(static_cast<std::size_t>(workers));

        for (int w = 0; w < workers; ++w) {
            handles.push_back(tf::Spawn([this] {
                while (true) {
                    const int idx = next_frame_.fetch_add(1);
                    if (idx >= total_frames_) {
                        return;
                    }
                    try {
                        pipeline_->ProcessFrame(idx);
                    } catch (const tf::SchedulerStoppingError&) {
                        return;
                    }
                    completed_frames_.fetch_add(1);
                    // Push progress before yielding so the very last frame's
                    // progress is observed even if the scheduler shuts down.
                    listener_->OnProgress(Progress());
                    try {
                        tf::Yield();
                    } catch (const tf::SchedulerStoppingError&) {
                        return;
                    }
                }
            }));
        }

        for (auto& h : handles) {
            try {
                h.Wait();
            } catch (const tf::SchedulerStoppingError&) {
                // Continue waiting on remaining workers under shutdown.
            }
        }
    });
}

bool CooperativeRunner::Step() {
    if (!scheduler_ || finished_) {
        return false;
    }

    const bool more = scheduler_->Step();
    if (!more) {
        DispatchCompletionIfDone();
    }
    return more;
}

void CooperativeRunner::Cancel() {
    if (!scheduler_ || finished_) {
        return;
    }
    cancelled_ = true;
    scheduler_->Stop();
}

float CooperativeRunner::Progress() const noexcept {
    if (total_frames_ <= 0) {
        return 1.0f;
    }
    const int done = completed_frames_.load();
    return std::min(1.0f, static_cast<float>(done) / static_cast<float>(total_frames_));
}

void CooperativeRunner::DispatchCompletionIfDone() {
    if (finished_) {
        return;
    }
    finished_ = true;
    if (cancelled_) {
        listener_->OnCancelled();
    } else {
        listener_->OnComplete();
    }
    scheduler_.reset();
}

} // namespace cortex::video_editor
