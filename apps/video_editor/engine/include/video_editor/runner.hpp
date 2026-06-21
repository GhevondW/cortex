#pragma once

namespace cortex::video_editor {

class Pipeline;
class IProgressListener;

// Strategy interface for "how is the bulk filter-apply scheduled?".
// CooperativeRunner uses tiny_fiber, BlockingRunner is a synchronous foil
// for the side-by-side freeze demo.
class IRunner {
public:
    virtual ~IRunner() = default;

    virtual void Start(Pipeline& pipeline, IProgressListener& listener) = 0;
    virtual bool Step() = 0;
    virtual void Cancel() = 0;

    [[nodiscard]] virtual bool IsRunning() const noexcept = 0;
    [[nodiscard]] virtual float Progress() const noexcept = 0;
};

} // namespace cortex::video_editor
