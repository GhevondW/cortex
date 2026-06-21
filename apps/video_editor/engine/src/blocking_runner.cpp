#include <video_editor/blocking_runner.hpp>

#include <video_editor/pipeline.hpp>

namespace cortex::video_editor {

void BlockingRunner::Start(Pipeline& pipeline, IProgressListener& listener) {
    cancel_requested_ = false;
    const int total = pipeline.FrameCount();
    if (total <= 0) {
        progress_ = 1.0f;
        listener.OnComplete();
        return;
    }

    for (int i = 0; i < total; ++i) {
        if (cancel_requested_) {
            listener.OnCancelled();
            return;
        }
        pipeline.ProcessFrame(i);
        progress_ = static_cast<float>(i + 1) / static_cast<float>(total);
        listener.OnProgress(progress_);
    }
    listener.OnComplete();
}

bool BlockingRunner::Step() {
    return false;
}

void BlockingRunner::Cancel() {
    // Note: under WASM single-threaded execution the Start() call has already
    // run to completion by the time JS can call Cancel(). The flag exists so
    // tests on a separate thread (or future async drivers) can interrupt.
    cancel_requested_ = true;
}

} // namespace cortex::video_editor
