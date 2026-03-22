#include "editor_session.hpp"

#include <cortex/config.hpp>

#include <cstdint>
#include <memory>

namespace cortex::image_editor::mvp {
namespace {
std::unique_ptr<EditorSession> g_session;

EditorSession& RequireSession() {
    if (!g_session) {
        g_session = std::make_unique<EditorSession>();
        g_session->Initialize();
    }
    return *g_session;
}

void DestroySession() {
    g_session.reset();
}
} // namespace
} // namespace cortex::image_editor::mvp

extern "C" {

CORTEX_API void editor_init() {
    auto& session = cortex::image_editor::mvp::RequireSession();
    session.Initialize();
}

CORTEX_API void editor_destroy() {
    cortex::image_editor::mvp::DestroySession();
}

CORTEX_API void editor_set_source_rgba(std::uintptr_t ptr, int width, int height, int stride_bytes) {
    auto& session = cortex::image_editor::mvp::RequireSession();
    session.SetSourceImage(reinterpret_cast<const std::uint8_t*>(ptr), width, height, stride_bytes);
}

CORTEX_API void editor_set_grayscale_amount(float value) {
    auto& session = cortex::image_editor::mvp::RequireSession();
    session.SetGrayscaleAmount(value);
}

CORTEX_API void editor_set_blur_radius(int value) {
    auto& session = cortex::image_editor::mvp::RequireSession();
    session.SetBlurRadius(value);
}

CORTEX_API int editor_pump(int max_steps) {
    auto& session = cortex::image_editor::mvp::RequireSession();
    return session.Pump(max_steps);
}

CORTEX_API int editor_needs_pump() {
    auto& session = cortex::image_editor::mvp::RequireSession();
    return session.NeedsPump() ? 1 : 0;
}

CORTEX_API int editor_get_status() {
    auto& session = cortex::image_editor::mvp::RequireSession();
    return static_cast<int>(session.GetStatus());
}

CORTEX_API int editor_get_output_width() {
    auto& session = cortex::image_editor::mvp::RequireSession();
    return session.GetOutputWidth();
}

CORTEX_API int editor_get_output_height() {
    auto& session = cortex::image_editor::mvp::RequireSession();
    return session.GetOutputHeight();
}

CORTEX_API std::uintptr_t editor_get_output_ptr() {
    auto& session = cortex::image_editor::mvp::RequireSession();
    return reinterpret_cast<std::uintptr_t>(session.GetOutputPixels());
}

CORTEX_API int editor_get_output_size() {
    auto& session = cortex::image_editor::mvp::RequireSession();
    return session.GetOutputSize();
}

CORTEX_API int editor_copy_output_rgba(std::uintptr_t ptr, int capacity) {
    auto& session = cortex::image_editor::mvp::RequireSession();
    return session.CopyOutputPixels(reinterpret_cast<std::uint8_t*>(ptr), capacity);
}

CORTEX_API int editor_get_output_revision() {
    auto& session = cortex::image_editor::mvp::RequireSession();
    return static_cast<int>(session.GetOutputRevision());
}

CORTEX_API const char* editor_get_error_message() {
    auto& session = cortex::image_editor::mvp::RequireSession();
    return session.GetErrorMessage();
}

} // extern "C"

int main() {
    return 0;
}
