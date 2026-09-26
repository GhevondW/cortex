#include <video_editor/live_cooperative.hpp>

#include <video_editor/filters/brightness.hpp>
#include <video_editor/filters/contrast.hpp>
#include <video_editor/filters/gaussian_blur.hpp>
#include <video_editor/filters/saturation.hpp>

#include <utility>

namespace cortex::video_editor {

namespace tf = cortex::tiny_fiber;

void BuildFilterChain(FilterChain& chain, const LiveFilterParams& params) {
    chain.Clear();
    // Order is fixed and chosen for visual sanity: tonal adjustments first
    // (brightness then contrast then saturation), spatial blur last so it
    // operates on the already-color-corrected frame.
    if (params.brightness != 0.0f) {
        chain.Add(std::make_unique<filters::BrightnessFilter>(params.brightness));
    }
    if (params.contrast != 1.0f) {
        chain.Add(std::make_unique<filters::ContrastFilter>(params.contrast));
    }
    if (params.saturation != 1.0f) {
        chain.Add(std::make_unique<filters::SaturationFilter>(params.saturation));
    }
    if (params.blur_radius > 0) {
        chain.Add(std::make_unique<filters::GaussianBlurFilter>(params.blur_radius));
    }
}

// Buffers and the chain are reused across renders (the editor begins a fresh
// render every displayed frame), so the steady state allocates no frames.
struct LiveCooperativeRenderer::State {
    FrameBuffer input;
    FrameBuffer output;
    FilterChain chain;
    LiveFilterParams chain_params;
    bool has_chain {false};
};

LiveCooperativeRenderer::LiveCooperativeRenderer(std::chrono::microseconds time_slice)
    : time_slice_(time_slice) {}

LiveCooperativeRenderer::~LiveCooperativeRenderer() = default;

void LiveCooperativeRenderer::Begin(const FrameBuffer& source, const LiveFilterParams& params) {
    // Drop any in-flight render first (tears the old scheduler down cleanly),
    // so its fiber no longer uses the chain or buffers changed below.
    scheduler_.reset();

    if (!state_) {
        state_ = std::make_shared<State>();
    }
    State& state = *state_;
    if (state.input.Width() != source.Width() || state.input.Height() != source.Height()) {
        state.input = FrameBuffer(source.Width(), source.Height());
        state.output = FrameBuffer(source.Width(), source.Height());
    }
    state.input.CopyFrom(source);
    if (!state.has_chain || !(state.chain_params == params)) {
        BuildFilterChain(state.chain, params);
        state.chain_params = params;
        state.has_chain = true;
    }
    done_ = false;

    tf::Scheduler::Config config;
    config.time_slice = time_slice_;
    auto shared = state_;
    scheduler_ = tf::Scheduler::Create([shared] { shared->chain.Apply(shared->input, shared->output); }, config);
}

bool LiveCooperativeRenderer::RunFor(std::chrono::microseconds budget) {
    if (!scheduler_ || done_) {
        return false;
    }
    scheduler_->RunFor(budget);
    return Settle();
}

bool LiveCooperativeRenderer::Step() {
    if (!scheduler_ || done_) {
        return false;
    }
    scheduler_->Step();
    return Settle();
}

bool LiveCooperativeRenderer::Settle() {
    if (!scheduler_->IsDone()) {
        return true;
    }
    done_ = true;
    scheduler_.reset();
    return false;
}

const FrameBuffer& LiveCooperativeRenderer::Output() const noexcept {
    return state_->output;
}

} // namespace cortex::video_editor
