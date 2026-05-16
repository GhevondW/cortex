// Thin object-oriented wrapper around the Module._editor_* C ABI. Other JS
// modules talk to this class, never directly to the Emscripten Module. Keeps
// the "this is the boundary" surface clear and makes it trivial to mock in
// future tests.

export class EditorClient {
    constructor(module) {
        this._mod = module;
    }

    init(width, height, frameCount) {
        return this._mod._editor_init(width, height, frameCount) === 1;
    }

    // Replace the active source with a freshly-generated procedural one.
    // Used by the "Reset to procedural" button.
    resetProcedural(width, height, frameCount) {
        return this._mod._editor_reset_procedural(width, height, frameCount) === 1;
    }

    // Replace the active source with a writable, zero-initialised uploaded
    // one of the given dimensions. After this returns true, call
    // writeSourceFrame(idx, rgbaBytes) for each frame.
    resetUploaded(width, height, frameCount) {
        return this._mod._editor_reset_uploaded(width, height, frameCount) === 1;
    }

    // Copies a frame's RGBA8 bytes into the WASM heap at the source slot
    // for `frameIdx`. `pixels` is a Uint8ClampedArray/Uint8Array sized
    // (width * height * 4). Throws if the heap pointer is null (no upload
    // is active or idx is out of range).
    writeSourceFrame(frameIdx, pixels) {
        const ptr = this._mod._editor_writable_source_pixels(frameIdx);
        if (!ptr) {
            throw new Error(`No writable buffer for source frame ${frameIdx}`);
        }
        this._mod.HEAPU8.set(pixels, ptr);
    }

    width()       { return this._mod._editor_get_width(); }
    height()      { return this._mod._editor_get_height(); }
    frameCount()  { return this._mod._editor_get_frame_count(); }

    setBrightness(v)  { this._mod._editor_set_brightness(v); }
    setContrast(v)    { this._mod._editor_set_contrast(v); }
    setSaturation(v)  { this._mod._editor_set_saturation(v); }
    setBlurRadius(r)  { this._mod._editor_set_blur_radius(r); }

    renderPreview(frameIdx) {
        this._mod._editor_render_preview(frameIdx);
    }

    startApplyCooperative() { this._mod._editor_start_apply_cooperative(); }
    runApplyBlocking()      { this._mod._editor_apply_blocking(); }
    step()                  { return this._mod._editor_step() === 1; }
    cancel()                { this._mod._editor_cancel(); }
    progress()              { return this._mod._editor_get_progress(); }

    // Returns a Uint8ClampedArray view into the WASM heap holding the
    // requested frame's RGBA pixels. The view is invalidated whenever the
    // heap grows; callers should not retain it across yields.
    sourceFrame(frameIdx) {
        const ptr = this._mod._editor_get_source_frame(frameIdx);
        return this._frameView(ptr);
    }
    outputFrame(frameIdx) {
        const ptr = this._mod._editor_get_output_frame(frameIdx);
        return this._frameView(ptr);
    }

    _frameView(ptr) {
        if (!ptr) return null;
        const size = this.width() * this.height() * 4;
        return new Uint8ClampedArray(this._mod.HEAPU8.buffer, ptr, size);
    }
}
