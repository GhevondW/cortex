#pragma once

#include <video_editor/filter_chain.hpp>
#include <video_editor/frame_buffer.hpp>

#include <cortex/tiny_fiber/scheduler.hpp>

#include <chrono>
#include <memory>

namespace cortex::video_editor {

// The four live-editor filter parameters, in the fixed order the editor applies
// them. Identity values (the defaults) are skipped.
struct LiveFilterParams {
    float brightness {0.0f}; // identity 0
    float contrast {1.0f}; // identity 1
    float saturation {1.0f}; // identity 1
    int blur_radius {0}; // identity 0

    bool operator==(const LiveFilterParams&) const = default;
};

// Fills `chain` with the filters for `params`: brightness, contrast,
// saturation, then blur, skipping identity stages. Used by both the Editor's
// synchronous chain and the cooperative renderer, so they cannot diverge.
void BuildFilterChain(FilterChain& chain, const LiveFilterParams& params);

// Applies the filter chain to ONE frame cooperatively: the real filters run in
// a cortex::tiny_fiber fiber and call tiny_fiber::CheckPoint() once per row,
// which yields whenever the time slice is spent — so even a heavy chain never
// blocks the calling thread for long. Drive it with RunFor() (or Step());
// Output() is valid once Done() returns true.
//
// This is the live editor's "Cortex cooperative engine" path — the synchronous
// foil is Editor::RenderPreview. It runs the very same filter code, so output
// is byte-identical (verified in live_cooperative_test.cpp).
class LiveCooperativeRenderer final {
public:
    // time_slice: how long the filters run before a CheckPoint() yields.
    explicit LiveCooperativeRenderer(std::chrono::microseconds time_slice = std::chrono::milliseconds(2));
    ~LiveCooperativeRenderer();

    LiveCooperativeRenderer(const LiveCooperativeRenderer&) = delete;
    LiveCooperativeRenderer& operator=(const LiveCooperativeRenderer&) = delete;
    LiveCooperativeRenderer(LiveCooperativeRenderer&&) = delete;
    LiveCooperativeRenderer& operator=(LiveCooperativeRenderer&&) = delete;

    // Start a new cooperative render of `source` through `params`. Copies what it
    // needs from `source`, so `source` need not outlive the call. Cancels any
    // in-flight render.
    void Begin(const FrameBuffer& source, const LiveFilterParams& params);

    // Run the render for up to `budget` of CPU time. Returns true while more
    // work remains.
    bool RunFor(std::chrono::microseconds budget);

    // Advance one scheduler step (one time slice). Returns true while more
    // work remains.
    bool Step();

    [[nodiscard]] bool Done() const noexcept {
        return done_;
    }

    // The filtered frame. Valid (complete) once Done() is true; partially filled
    // before then.
    [[nodiscard]] const FrameBuffer& Output() const noexcept;

private:
    struct State;

    // Record completion after the scheduler ran; returns true while more work
    // remains.
    bool Settle();

    std::chrono::microseconds time_slice_;
    std::shared_ptr<State> state_;
    std::unique_ptr<cortex::tiny_fiber::Scheduler> scheduler_;
    bool done_ {true};
};

} // namespace cortex::video_editor
