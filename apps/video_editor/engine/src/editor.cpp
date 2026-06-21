#include <video_editor/editor.hpp>

#include <video_editor/blocking_runner.hpp>
#include <video_editor/cooperative_runner.hpp>
#include <video_editor/filter_chain.hpp>
#include <video_editor/filters/brightness.hpp>
#include <video_editor/filters/contrast.hpp>
#include <video_editor/filters/gaussian_blur.hpp>
#include <video_editor/filters/saturation.hpp>
#include <video_editor/live_cooperative.hpp>
#include <video_editor/pipeline.hpp>
#include <video_editor/procedural_source.hpp>
#include <video_editor/uploaded_source.hpp>

#include <algorithm>

namespace cortex::video_editor {

class Editor::Impl {
public:
    Impl(int width, int height, int frame_count) {
        InstallProcedural(width, height, frame_count);
        RebuildChain();
        pipeline_ = std::make_unique<Pipeline>(*source_, chain_);
    }

    int Width() const noexcept {
        return source_->Width();
    }
    int Height() const noexcept {
        return source_->Height();
    }
    int FrameCount() const noexcept {
        return source_->FrameCount();
    }
    const FrameBuffer& Source(int idx) const {
        return source_->At(idx);
    }
    const FrameBuffer& Output(int idx) const {
        return pipeline_->OutputAt(idx);
    }

    void ResetProcedural(int width, int height, int frame_count) {
        TearDownRunner();
        InstallProcedural(width, height, frame_count);
        pipeline_ = std::make_unique<Pipeline>(*source_, chain_);
    }

    void ResetUploaded(int width, int height, int frame_count) {
        TearDownRunner();
        InstallUploaded(width, height, frame_count);
        pipeline_ = std::make_unique<Pipeline>(*source_, chain_);
    }

    std::uint8_t* WritableSourcePixels(int idx) noexcept {
        return uploaded_ ? uploaded_->WritablePixels(idx) : nullptr;
    }

    void SetBrightness(float v) {
        brightness_ = std::clamp(v, -1.0f, 1.0f);
        chain_dirty_ = true;
    }
    void SetContrast(float v) {
        contrast_ = std::clamp(v, 0.0f, 2.0f);
        chain_dirty_ = true;
    }
    void SetSaturation(float v) {
        saturation_ = std::clamp(v, 0.0f, 2.0f);
        chain_dirty_ = true;
    }
    void SetBlurRadius(int r) {
        blur_radius_ = std::clamp(r, 0, 32);
        chain_dirty_ = true;
    }

    void EnsureChain() {
        if (chain_dirty_) {
            RebuildChain();
            chain_dirty_ = false;
        }
    }

    void RenderPreview(int idx) {
        EnsureChain();
        pipeline_->RenderPreview(idx);
    }

    void BeginCooperativeRender(int idx) {
        // Reuse the renderer (and its frame buffers) across frames; the editor
        // begins a fresh cooperative render every displayed frame.
        if (!coop_renderer_) {
            coop_renderer_ = std::make_unique<LiveCooperativeRenderer>();
        }
        coop_idx_ = idx;
        coop_done_ = false;
        const LiveFilterParams params {brightness_, contrast_, saturation_, blur_radius_};
        coop_renderer_->Begin(source_->At(idx), params);
    }

    bool StepCooperative() {
        if (!coop_renderer_ || coop_done_) {
            return false;
        }
        const bool more = coop_renderer_->Step();
        if (!more) {
            // Publish the finished frame so Output(idx) / the bridge see it.
            // Keep the renderer alive so the next frame's Begin reuses its buffers.
            pipeline_->WriteOutput(coop_idx_, coop_renderer_->Output());
            coop_done_ = true;
        }
        return more;
    }

    bool CooperativeRenderDone() const noexcept {
        return coop_done_;
    }

    void StartCooperativeApply(IProgressListener& listener) {
        EnsureChain();
        runner_ = std::make_unique<CooperativeRunner>();
        runner_->Start(*pipeline_, listener);
    }

    void RunBlockingApply(IProgressListener& listener) {
        EnsureChain();
        runner_ = std::make_unique<BlockingRunner>();
        runner_->Start(*pipeline_, listener);
    }

    bool Step() {
        return runner_ ? runner_->Step() : false;
    }

    void Cancel() {
        if (runner_) {
            runner_->Cancel();
        }
    }

    float Progress() const noexcept {
        return runner_ ? runner_->Progress() : 1.0f;
    }

private:
    void RebuildChain() {
        chain_.Clear();
        // Order is fixed and chosen for visual sanity: tonal adjustments
        // first (brightness then contrast then saturation), spatial blur last
        // so it operates on the already-color-corrected frame.
        if (brightness_ != 0.0f) {
            chain_.Add(std::make_unique<filters::BrightnessFilter>(brightness_));
        }
        if (contrast_ != 1.0f) {
            chain_.Add(std::make_unique<filters::ContrastFilter>(contrast_));
        }
        if (saturation_ != 1.0f) {
            chain_.Add(std::make_unique<filters::SaturationFilter>(saturation_));
        }
        if (blur_radius_ > 0) {
            chain_.Add(std::make_unique<filters::GaussianBlurFilter>(blur_radius_));
        }
    }

    void InstallProcedural(int w, int h, int n) {
        source_ = std::make_unique<ProceduralFrameSource>(w, h, n);
        uploaded_ = nullptr;
    }

    void InstallUploaded(int w, int h, int n) {
        auto src = std::make_unique<UploadedFrameSource>(w, h, n);
        uploaded_ = src.get();
        source_ = std::move(src);
    }

    void TearDownRunner() {
        // Make sure any cooperative scheduler still owning a reference to the
        // old pipeline is torn down before the source/pipeline is replaced.
        if (runner_) {
            runner_->Cancel();
            runner_.reset();
        }
        // A live cooperative render writes into the pipeline output, so drop it
        // too before the pipeline is swapped out from under it.
        coop_renderer_.reset();
        coop_done_ = true;
    }

    std::unique_ptr<IFrameSource> source_;
    UploadedFrameSource* uploaded_ {nullptr}; // non-owning; valid iff source_ is uploaded
    FilterChain chain_;
    std::unique_ptr<Pipeline> pipeline_;
    std::unique_ptr<IRunner> runner_;
    std::unique_ptr<LiveCooperativeRenderer> coop_renderer_;
    int coop_idx_ {0};
    bool coop_done_ {true};

    float brightness_ {0.0f};
    float contrast_ {1.0f};
    float saturation_ {1.0f};
    int blur_radius_ {0};
    bool chain_dirty_ {false};
};

std::unique_ptr<Editor> Editor::Create(int width, int height, int frame_count) {
    auto impl = std::make_unique<Impl>(width, height, frame_count);
    return std::unique_ptr<Editor>(new Editor(std::move(impl)));
}

Editor::Editor(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

Editor::~Editor() = default;

int Editor::Width() const noexcept {
    return impl_->Width();
}
int Editor::Height() const noexcept {
    return impl_->Height();
}
int Editor::FrameCount() const noexcept {
    return impl_->FrameCount();
}
const FrameBuffer& Editor::Source(int idx) const {
    return impl_->Source(idx);
}
const FrameBuffer& Editor::Output(int idx) const {
    return impl_->Output(idx);
}
void Editor::ResetProcedural(int width, int height, int frame_count) {
    impl_->ResetProcedural(width, height, frame_count);
}
void Editor::ResetUploaded(int width, int height, int frame_count) {
    impl_->ResetUploaded(width, height, frame_count);
}
std::uint8_t* Editor::WritableSourcePixels(int idx) noexcept {
    return impl_->WritableSourcePixels(idx);
}
void Editor::SetBrightness(float v) {
    impl_->SetBrightness(v);
}
void Editor::SetContrast(float v) {
    impl_->SetContrast(v);
}
void Editor::SetSaturation(float v) {
    impl_->SetSaturation(v);
}
void Editor::SetBlurRadius(int r) {
    impl_->SetBlurRadius(r);
}
void Editor::RenderPreview(int idx) {
    impl_->RenderPreview(idx);
}
void Editor::BeginCooperativeRender(int frame_idx) {
    impl_->BeginCooperativeRender(frame_idx);
}
bool Editor::StepCooperative() {
    return impl_->StepCooperative();
}
bool Editor::CooperativeRenderDone() const noexcept {
    return impl_->CooperativeRenderDone();
}
void Editor::StartCooperativeApply(IProgressListener& l) {
    impl_->StartCooperativeApply(l);
}
void Editor::RunBlockingApply(IProgressListener& l) {
    impl_->RunBlockingApply(l);
}
bool Editor::Step() {
    return impl_->Step();
}
void Editor::Cancel() {
    impl_->Cancel();
}
float Editor::Progress() const noexcept {
    return impl_->Progress();
}

} // namespace cortex::video_editor
