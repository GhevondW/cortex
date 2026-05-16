#pragma once

#include <video_editor/frame_buffer.hpp>
#include <video_editor/progress_listener.hpp>

#include <memory>

namespace cortex::video_editor {

// Facade for the entire video-editor engine. The WASM bridge interacts only
// with this class; concrete pipeline / runner / filter types stay
// implementation-private behind a PIMPL.
class Editor final {
public:
    static std::unique_ptr<Editor> Create(int width, int height, int frame_count);
    ~Editor();

    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;
    Editor(Editor&&) = delete;
    Editor& operator=(Editor&&) = delete;

    [[nodiscard]] int Width() const noexcept;
    [[nodiscard]] int Height() const noexcept;
    [[nodiscard]] int FrameCount() const noexcept;

    [[nodiscard]] const FrameBuffer& Source(int idx) const;
    [[nodiscard]] const FrameBuffer& Output(int idx) const;

    // Filter parameters. Setters do not re-render; call RenderPreview() to
    // see the result on the currently-selected frame.
    void SetBrightness(float v); // -1.0 .. 1.0
    void SetContrast(float v); //  0.0 .. 2.0
    void SetSaturation(float v); //  0.0 .. 2.0
    void SetBlurRadius(int r); //  0 .. 32

    // Run the current filter chain on a single source frame and write to the
    // matching output buffer. Used by the live-preview path.
    void RenderPreview(int frame_idx);

    // Bulk apply across all frames. Cooperative uses tiny_fiber and yields to
    // JS between frames; blocking runs to completion synchronously.
    void StartCooperativeApply(IProgressListener& listener);
    void RunBlockingApply(IProgressListener& listener);

    // Advance the cooperative runner one Step. Returns true if more work is
    // pending. No-op when no run is in progress.
    bool Step();

    // Signal the cooperative runner to stop; runners exit at the next yield.
    void Cancel();

    [[nodiscard]] float Progress() const noexcept;

private:
    class Impl;
    explicit Editor(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace cortex::video_editor
