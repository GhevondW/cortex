#pragma once

namespace cortex::video_editor {

// Observer interface for bulk-apply runners to report status. Implemented by
// the WASM bridge (forwards to EM_JS) and by RecordingListener in tests.
class IProgressListener {
public:
    virtual ~IProgressListener() = default;

    // pct in [0.0, 1.0]
    virtual void OnProgress(float pct) = 0;
    virtual void OnComplete() = 0;
    virtual void OnCancelled() = 0;
};

} // namespace cortex::video_editor
