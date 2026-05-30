#pragma once

#include <video_editor/frame_buffer.hpp>
#include <video_editor/progress_listener.hpp>

#include <cstdint>
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

    // Replace the active source with a freshly-generated procedural one.
    // Cancels any in-flight runner and resizes internal buffers. Throws on
    // non-positive dimensions.
    void ResetProcedural(int width, int height, int frame_count);

    // Replace the active source with a writable, zero-initialised uploaded
    // source. Callers populate each frame via WritableSourcePixels(idx).
    // Cancels any in-flight runner. Throws on non-positive dimensions.
    void ResetUploaded(int width, int height, int frame_count);

    // Returns a pointer to source frame idx's RGBA8 pixel buffer for in-place
    // population by the upload flow, or nullptr if the active source is
    // read-only (e.g. procedural) or idx is out of range.
    [[nodiscard]] std::uint8_t* WritableSourcePixels(int idx) noexcept;

    // Filter parameters. Setters do not re-render; call RenderPreview() to
    // see the result on the currently-selected frame.
    void SetBrightness(float v); // -1.0 .. 1.0
    void SetContrast(float v); //  0.0 .. 2.0
    void SetSaturation(float v); //  0.0 .. 2.0
    void SetBlurRadius(int r); //  0 .. 32

    // Run the current filter chain on a single source frame and write to the
    // matching output buffer. Used by the live-preview path.
    void RenderPreview(int frame_idx);

    // Cooperative single-frame render: filters frame_idx through the current
    // parameters via tiny_fiber, split into horizontal row-bands that yield, so
    // a heavy filter never blocks the main thread. Drive with StepCooperative()
    // until CooperativeRenderDone() returns true; the finished frame lands in
    // Output(frame_idx), byte-identical to RenderPreview(frame_idx).
    void BeginCooperativeRender(int frame_idx);
    bool StepCooperative();
    [[nodiscard]] bool CooperativeRenderDone() const noexcept;

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
