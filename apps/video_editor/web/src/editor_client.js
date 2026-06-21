// Thin object-oriented wrapper around the Module._editor_* C ABI. Other JS
// modules talk to this class, never directly to the Emscripten Module. Keeps
// the "this is the boundary" surface clear and makes it trivial to mock.

export class EditorClient {
    constructor(module) {
        this._mod = module;
    }

    init(width, height, frameCount) {
        return this._mod._editor_init(width, height, frameCount) === 1;
    }

    // Replace the active source with a freshly-generated procedural one.
    resetProcedural(width, height, frameCount) {
        return this._mod._editor_reset_procedural(width, height, frameCount) === 1;
    }

    // Replace the active source with a writable, zero-initialised uploaded one.
    // For the live editor this is called with frameCount === 1: a single
    // ping-pong slot that each displayed video frame is written into.
    resetUploaded(width, height, frameCount) {
        return this._mod._editor_reset_uploaded(width, height, frameCount) === 1;
    }

    // Copy a frame's RGBA8 bytes into the WASM heap at the source slot for
    // `frameIdx`. `pixels` is a Uint8ClampedArray/Uint8Array of (w*h*4) bytes.
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

    // Run the filter chain synchronously on one frame (source[idx] -> output[idx]).
    renderPreview(frameIdx) {
        this._mod._editor_render_preview(frameIdx);
    }

    // --- Cooperative single-frame render (Phase 2). Present only if the WASM
    // build exports the functions; supportsCooperative() guards every caller. ---
    supportsCooperative() {
        return typeof this._mod._editor_begin_cooperative_render === "function"
            && typeof this._mod._editor_step_cooperative === "function"
            && typeof this._mod._editor_cooperative_done === "function";
    }
    beginCooperativeRender(frameIdx) { this._mod._editor_begin_cooperative_render(frameIdx); }
    stepCooperative()                { return this._mod._editor_step_cooperative() === 1; }
    cooperativeDone()                { return this._mod._editor_cooperative_done() === 1; }

    // Returns a Uint8ClampedArray view into the WASM heap holding the requested
    // frame's RGBA pixels. The view is invalidated whenever the heap grows;
    // callers must not retain it across calls that may allocate.
    sourceFrame(frameIdx) {
        return this._frameView(this._mod._editor_get_source_frame(frameIdx));
    }
    outputFrame(frameIdx) {
        return this._frameView(this._mod._editor_get_output_frame(frameIdx));
    }

    _frameView(ptr) {
        if (!ptr) return null;
        const size = this.width() * this.height() * 4;
        return new Uint8ClampedArray(this._mod.HEAPU8.buffer, ptr, size);
    }
}
