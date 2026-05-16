// WebAssembly bridge for the video_editor demo.
//
// Pure translation: forwards extern "C" calls into the Editor facade and
// implements IProgressListener by relaying events into JS via EM_JS. No
// editor logic lives here.

#include <video_editor/editor.hpp>
#include <video_editor/progress_listener.hpp>

#include <cortex/config.hpp>

#include <cstdint>
#include <memory>

#ifdef CORTEX_EMSCRIPTEN
#include <emscripten/emscripten.h>
#endif

namespace {

using cortex::video_editor::Editor;
using cortex::video_editor::FrameBuffer;
using cortex::video_editor::IProgressListener;

std::unique_ptr<Editor> g_editor;

#ifdef CORTEX_EMSCRIPTEN
// clang-format off
EM_JS(void, js_on_progress, (float pct), {
    if (typeof onApplyProgress === 'function') {
        onApplyProgress(pct);
    }
});
EM_JS(void, js_on_complete, (), {
    if (typeof onApplyComplete === 'function') {
        onApplyComplete();
    }
});
EM_JS(void, js_on_cancelled, (), {
    if (typeof onApplyCancelled === 'function') {
        onApplyCancelled();
    }
});
// clang-format on
#else
// Stubs so the bridge compiles natively (unused; the WASM build is the real target).
inline void js_on_progress(float) {}
inline void js_on_complete() {}
inline void js_on_cancelled() {}
#endif

class JsProgressListener final : public IProgressListener {
public:
    void OnProgress(float pct) override {
        js_on_progress(pct);
    }
    void OnComplete() override {
        js_on_complete();
    }
    void OnCancelled() override {
        js_on_cancelled();
    }
};

JsProgressListener g_listener;

} // namespace

extern "C" {

CORTEX_API int editor_init(int width, int height, int frame_count) {
    g_editor = Editor::Create(width, height, frame_count);
    return g_editor ? 1 : 0;
}

CORTEX_API void editor_shutdown() {
    g_editor.reset();
}

CORTEX_API int editor_get_width() {
    return g_editor ? g_editor->Width() : 0;
}

CORTEX_API int editor_get_height() {
    return g_editor ? g_editor->Height() : 0;
}

CORTEX_API int editor_get_frame_count() {
    return g_editor ? g_editor->FrameCount() : 0;
}

CORTEX_API const std::uint8_t* editor_get_source_frame(int idx) {
    return g_editor ? g_editor->Source(idx).Data() : nullptr;
}

CORTEX_API const std::uint8_t* editor_get_output_frame(int idx) {
    return g_editor ? g_editor->Output(idx).Data() : nullptr;
}

CORTEX_API void editor_set_brightness(float v) {
    if (g_editor) g_editor->SetBrightness(v);
}

CORTEX_API void editor_set_contrast(float v) {
    if (g_editor) g_editor->SetContrast(v);
}

CORTEX_API void editor_set_saturation(float v) {
    if (g_editor) g_editor->SetSaturation(v);
}

CORTEX_API void editor_set_blur_radius(int r) {
    if (g_editor) g_editor->SetBlurRadius(r);
}

CORTEX_API void editor_render_preview(int idx) {
    if (g_editor) g_editor->RenderPreview(idx);
}

CORTEX_API void editor_start_apply_cooperative() {
    if (g_editor) g_editor->StartCooperativeApply(g_listener);
}

CORTEX_API void editor_apply_blocking() {
    if (g_editor) g_editor->RunBlockingApply(g_listener);
}

CORTEX_API int editor_step() {
    if (!g_editor) return 0;
    return g_editor->Step() ? 1 : 0;
}

CORTEX_API float editor_get_progress() {
    return g_editor ? g_editor->Progress() : 1.0f;
}

CORTEX_API void editor_cancel() {
    if (g_editor) g_editor->Cancel();
}

} // extern "C"
