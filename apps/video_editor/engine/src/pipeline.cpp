#include <video_editor/pipeline.hpp>

#include <stdexcept>

namespace cortex::video_editor {

Pipeline::Pipeline(const IFrameSource& source, const FilterChain& chain)
    : source_(source)
    , chain_(chain) {
    outputs_.reserve(static_cast<std::size_t>(source.FrameCount()));
    for (int i = 0; i < source.FrameCount(); ++i) {
        outputs_.emplace_back(source.Width(), source.Height());
    }
}

int Pipeline::Width() const noexcept {
    return source_.Width();
}

int Pipeline::Height() const noexcept {
    return source_.Height();
}

int Pipeline::FrameCount() const noexcept {
    return static_cast<int>(outputs_.size());
}

const FrameBuffer& Pipeline::OutputAt(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(outputs_.size())) {
        throw std::out_of_range("Pipeline::OutputAt: index out of range");
    }
    return outputs_[static_cast<std::size_t>(idx)];
}

void Pipeline::ProcessFrame(int idx) {
    if (idx < 0 || idx >= static_cast<int>(outputs_.size())) {
        throw std::out_of_range("Pipeline::ProcessFrame: index out of range");
    }
    chain_.Apply(source_.At(idx), outputs_[static_cast<std::size_t>(idx)]);
}

void Pipeline::WriteOutput(int idx, const FrameBuffer& frame) {
    if (idx < 0 || idx >= static_cast<int>(outputs_.size())) {
        throw std::out_of_range("Pipeline::WriteOutput: index out of range");
    }
    outputs_[static_cast<std::size_t>(idx)].CopyFrom(frame);
}

} // namespace cortex::video_editor
