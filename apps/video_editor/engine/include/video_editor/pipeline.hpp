#pragma once

#include <video_editor/filter_chain.hpp>
#include <video_editor/frame_buffer.hpp>
#include <video_editor/frame_source.hpp>

#include <vector>

namespace cortex::video_editor {

// Orchestrates the source → chain → output transformation. Holds references
// to the source and chain (caller-owned) and owns the output frame storage.
// Runners call ProcessFrame() per frame; the UI calls RenderPreview() for
// fast single-frame updates outside the bulk-apply path.
class Pipeline final {
public:
    Pipeline(const IFrameSource& source, const FilterChain& chain);

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    Pipeline(Pipeline&&) = delete;
    Pipeline& operator=(Pipeline&&) = delete;

    [[nodiscard]] int Width() const noexcept;
    [[nodiscard]] int Height() const noexcept;
    [[nodiscard]] int FrameCount() const noexcept;

    [[nodiscard]] const FrameBuffer& OutputAt(int idx) const;

    // Processes a single frame: reads source[idx], runs the chain, writes to
    // output[idx]. Used by both the preview path (foreground) and runners.
    void ProcessFrame(int idx);

    // Convenience: process the current preview frame. Identical to
    // ProcessFrame today; kept as a separate method so a future caching
    // strategy can diverge.
    void RenderPreview(int idx) {
        ProcessFrame(idx);
    }

private:
    const IFrameSource& source_;
    const FilterChain& chain_;
    std::vector<FrameBuffer> outputs_;
};

} // namespace cortex::video_editor
