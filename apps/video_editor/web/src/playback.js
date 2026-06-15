// playback.js
//
// Drives the per-displayed-frame loop that makes the editor "real time":
//   1. ask the active FrameProvider for the current frame (it writes pixels into
//      the engine for a video source, or just returns an index for procedural),
//   2. run the filter chain on that frame in WASM,
//   3. paint the filtered output to the canvas.
//
// While a source is playing we render every tick (each tick is a fresh frame);
// while paused we render only when something changed (a filter moved or a seek),
// tracked by a dirty flag. When the provider exposes requestVideoFrameCallback
// and is playing, we drive off it (one callback per displayed video frame, so
// the loop naturally drops frames it can't keep up with); otherwise rAF.

export class Playback {
    constructor({ client, canvas, onPosition }) {
        this._client = client;
        this._canvas = canvas;
        this._onPosition = onPosition || (() => {});
        this._provider = null;
        this._running = false;
        this._dirty = true;       // force the first paint
        this._rafId = 0;
        this._vfcHandle = 0;      // pending requestVideoFrameCallback handle
        this._vfcProvider = null; // provider that owns the pending rVFC

        // Phase 2 (cooperative engine) hooks — inert until enabled.
        this._cooperative = false;
        this._coopActive = false; // a cooperative render is in flight
        this._coopIndex = 0;
        this._coopStallTicks = 0;
    }

    setProvider(provider) {
        this._provider = provider;
        this._dirty = true;
        this._coopActive = false;
        // Re-kick scheduling. A pending requestVideoFrameCallback is bound to the
        // previous provider's <video>, which the caller is about to dispose — it
        // would never fire and the loop would stall (this is why opening a second
        // video used to hang until a page reload). Cancel it and schedule a fresh
        // tick against the new provider.
        if (this._running) {
            this._cancelPending();
            this._schedule();
        }
    }

    setOnPosition(fn) { this._onPosition = fn || (() => {}); }

    setCooperative(on) {
        this._cooperative = !!on;
        this._coopActive = false;  // restart cleanly on mode change
        this._dirty = true;
    }

    markDirty() { this._dirty = true; }

    start() {
        if (this._running || !this._provider) return;
        this._running = true;
        this._schedule();
    }

    stop() {
        this._running = false;
        this._cancelPending();
    }

    _schedule() {
        if (!this._running) return;
        const p = this._provider;
        if (p && p.hasVideoFrameCallback && p.playing) {
            this._vfcProvider = p;
            this._vfcHandle = p.requestVideoFrame(() => { this._vfcHandle = 0; this._tick(); });
        } else {
            this._rafId = requestAnimationFrame(() => { this._rafId = 0; this._tick(); });
        }
    }

    // Cancel whichever next-tick callback is currently pending (rAF or rVFC). The
    // rVFC handle is cancelled on the provider that owns it, since it is tied to
    // that provider's <video>.
    _cancelPending() {
        if (this._rafId) {
            cancelAnimationFrame(this._rafId);
            this._rafId = 0;
        }
        if (this._vfcHandle && this._vfcProvider) {
            this._vfcProvider.cancelVideoFrame(this._vfcHandle);
        }
        this._vfcHandle = 0;
        this._vfcProvider = null;
    }

    _tick() {
        if (!this._running) { return; }
        const p = this._provider;
        if (!p) { this._schedule(); return; }

        const playing = p.playing;
        const needRender = this._dirty || playing || this._coopActive;

        if (needRender) {
            if (this._cooperative) {
                this._renderCooperative(p);
            } else {
                this._renderSync(p);
            }
        }

        this._onPosition(p.position);
        this._schedule();
    }

    _renderSync(p) {
        const idx = p.produce(this._client);
        this._client.renderPreview(idx);
        this._paint(idx);
        this._dirty = false;
    }

    // Cooperative path: a single frame's filtering is stepped across ticks via
    // tiny_fiber so a heavy filter never blocks the main thread. We begin a new
    // cooperative render when none is active, pump it within a time budget each
    // tick, and only paint when it completes. A watchdog falls back to a sync
    // render if a frame fails to converge.
    _renderCooperative(p) {
        const client = this._client;
        if (!this._coopActive) {
            this._coopIndex = p.produce(client);
            client.beginCooperativeRender(this._coopIndex);
            this._coopActive = true;
            this._coopStallTicks = 0;
        }

        // Per-tick compute budget. ~12 ms uses most of a 60 fps frame while leaving
        // headroom for the browser to paint and stay responsive. A bigger budget
        // lets each frame's filtering finish in fewer ticks, keeping the cooperative
        // path closer to the synchronous one on heavy blur (it is inherently a bit
        // slower — it deliberately yields so the page never freezes).
        const budgetMs = 12;
        const start = performance.now();
        let done = client.cooperativeDone();
        while (!done && performance.now() - start < budgetMs) {
            client.stepCooperative();
            done = client.cooperativeDone();
        }

        if (done) {
            this._paint(this._coopIndex);
            this._coopActive = false;
            this._dirty = false;
        } else if (++this._coopStallTicks > 240) {
            // ~4s without converging: fall back so the editor never wedges.
            client.renderPreview(this._coopIndex);
            this._paint(this._coopIndex);
            this._coopActive = false;
            this._dirty = false;
            this._coopStallTicks = 0;
        }
    }

    _paint(idx) {
        const pixels = this._client.outputFrame(idx);
        this._canvas.draw(pixels, this._client.width(), this._client.height());
    }
}
