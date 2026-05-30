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
        if (this._rafId) {
            cancelAnimationFrame(this._rafId);
            this._rafId = 0;
        }
    }

    _schedule() {
        if (!this._running) return;
        const p = this._provider;
        if (p && p.hasVideoFrameCallback && p.playing) {
            p.requestVideoFrame(() => this._tick());
        } else {
            this._rafId = requestAnimationFrame(() => this._tick());
        }
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

        const budgetMs = 8;
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
