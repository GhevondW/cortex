#include <video_editor/editor.hpp>

#include <video_editor/blocking_runner.hpp>
#include <video_editor/cooperative_runner.hpp>
#include <video_editor/filter_chain.hpp>
#include <video_editor/filters/brightness.hpp>
#include <video_editor/filters/contrast.hpp>
#include <video_editor/filters/gaussian_blur.hpp>
#include <video_editor/filters/saturation.hpp>
#include <video_editor/pipeline.hpp>
#include <video_editor/procedural_source.hpp>

#include <algorithm>

namespace cortex::video_editor {

class Editor::Impl {
public:
    Impl(int width, int height, int frame_count)
        : source_(width, height, frame_count) {
        RebuildChain();
        pipeline_ = std::make_unique<Pipeline>(source_, chain_);
    }

    int Width() const noexcept {
        return source_.Width();
    }
    int Height() const noexcept {
        return source_.Height();
    }
    int FrameCount() const noexcept {
        return source_.FrameCount();
    }
    const FrameBuffer& Source(int idx) const {
        return source_.At(idx);
    }
    const FrameBuffer& Output(int idx) const {
        return pipeline_->OutputAt(idx);
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

    ProceduralFrameSource source_;
    FilterChain chain_;
    std::unique_ptr<Pipeline> pipeline_;
    std::unique_ptr<IRunner> runner_;

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
