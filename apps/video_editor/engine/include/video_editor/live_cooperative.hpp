#pragma once

#include <video_editor/frame_buffer.hpp>

#include <cortex/tiny_fiber/scheduler.hpp>

#include <memory>

namespace cortex::video_editor {

// The four live-editor filter parameters, in the fixed order the editor applies
// them. Identity values (the defaults) are skipped, exactly matching
// Editor::RebuildChain, so the cooperative output is byte-identical to the
// synchronous FilterChain for the same parameters.
struct LiveFilterParams {
    float brightness {0.0f}; // identity 0
    float contrast {1.0f}; // identity 1
    float saturation {1.0f}; // identity 1
    int blur_radius {0}; // identity 0
};

// Applies the filter chain to ONE frame cooperatively: the work is split into
// horizontal row-bands and yields between them via cortex::tiny_fiber, so even a
// heavy chain never blocks the calling thread for long. Drive it with Step()
// (one scheduler step per call); Output() is valid once Done() returns true.
//
// This is the live editor's "Cortex cooperative engine" path — the synchronous
// foil is Editor::RenderPreview. Output is byte-identical to the synchronous
// chain (verified in live_cooperative_test.cpp).
class LiveCooperativeRenderer final {
public:
    explicit LiveCooperativeRenderer(int band_rows = 16);
    ~LiveCooperativeRenderer();

    LiveCooperativeRenderer(const LiveCooperativeRenderer&) = delete;
    LiveCooperativeRenderer& operator=(const LiveCooperativeRenderer&) = delete;
    LiveCooperativeRenderer(LiveCooperativeRenderer&&) = delete;
    LiveCooperativeRenderer& operator=(LiveCooperativeRenderer&&) = delete;

    // Start a new cooperative render of `source` through `params`. Copies what it
    // needs from `source`, so `source` need not outlive the call. Cancels any
    // in-flight render.
    void Begin(const FrameBuffer& source, const LiveFilterParams& params);

    // Advance one scheduler step. Returns true while more work remains.
    bool Step();

    [[nodiscard]] bool Done() const noexcept {
        return done_;
    }

    // The filtered frame. Valid (complete) once Done() is true; partially filled
    // before then.
    [[nodiscard]] const FrameBuffer& Output() const noexcept;

private:
    struct State;
    int band_rows_;
    std::shared_ptr<State> state_;
    std::unique_ptr<cortex::tiny_fiber::Scheduler> scheduler_;
    bool done_ {true};
};

} // namespace cortex::video_editor
