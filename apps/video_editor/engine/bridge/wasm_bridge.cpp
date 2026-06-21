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

// Replace the active source with a freshly-generated procedural one. Returns
// 1 on success, 0 on bad arguments or missing editor. Used by the "reset to
// procedural" button after an upload.
CORTEX_API int editor_reset_procedural(int width, int height, int frame_count) {
    if (!g_editor || width <= 0 || height <= 0 || frame_count <= 0) {
        return 0;
    }
    g_editor->ResetProcedural(width, height, frame_count);
    return 1;
}

// Replace the active source with a writable, zero-initialised uploaded one.
// After this returns 1, JS should fill each frame's pixels by writing into
// the pointer returned by editor_writable_source_pixels(idx).
CORTEX_API int editor_reset_uploaded(int width, int height, int frame_count) {
    if (!g_editor || width <= 0 || height <= 0 || frame_count <= 0) {
        return 0;
    }
    g_editor->ResetUploaded(width, height, frame_count);
    return 1;
}

// Returns a writable pointer into the source frame's RGBA8 pixel buffer, or
// nullptr if no upload is active (procedural source) or idx is out of range.
// The pointed-to region is exactly (Width() * Height() * 4) bytes.
CORTEX_API std::uint8_t* editor_writable_source_pixels(int idx) {
    return g_editor ? g_editor->WritableSourcePixels(idx) : nullptr;
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

// Cooperative single-frame render. begin → step until done → read the result via
// editor_get_output_frame(idx). Lets a heavy filter run without freezing the page.
CORTEX_API void editor_begin_cooperative_render(int idx) {
    if (g_editor) g_editor->BeginCooperativeRender(idx);
}

CORTEX_API int editor_step_cooperative() {
    return (g_editor && g_editor->StepCooperative()) ? 1 : 0;
}

CORTEX_API int editor_cooperative_done() {
    return (!g_editor || g_editor->CooperativeRenderDone()) ? 1 : 0;
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

// Emscripten expects an entry point even though all editor lifecycle is
// driven by JS calls into the exported functions above. Return 0 so the
// runtime treats startup as successful; -sEXIT_RUNTIME=0 keeps the module
// alive afterwards.
int main() {
    return 0;
}
