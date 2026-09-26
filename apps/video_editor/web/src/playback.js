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

// Cooperative engine: longest a slice of filtering may hold the main thread.
// Slices run back to back between display ticks, so a frame's filtering uses
// all the idle time instead of one slice per tick, and the browser still gets
// the thread every few milliseconds to paint and handle input.
const COOPERATIVE_SLICE_MS = 8;

// Queue `callback` as a new macrotask (the same technique as js/cortex.mjs):
// a MessageChannel message in browsers, setImmediate elsewhere.
function makeMacrotaskQueue(callback) {
    if (typeof setImmediate === "function") {
        return { post: () => setImmediate(callback) };
    }
    const channel = new MessageChannel();
    channel.port1.onmessage = callback;
    return { post: () => channel.port2.postMessage(null) };
}

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
        this._coopQueued = false; // a newer frame arrived while it ran
        this._coopIndex = 0;
        this._pumpQueued = false;
        this._pump = makeMacrotaskQueue(() => {
            this._pumpQueued = false;
            this._pumpCooperative();
        });
    }

    setProvider(provider) {
        this._provider = provider;
        this._dirty = true;
        this._coopActive = false;
        this._coopQueued = false;
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
        this._coopQueued = false;
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

        const needRender = this._dirty || p.playing;
        if (needRender) {
            if (this._cooperative) {
                this._requestCooperative(p);
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

    // Cooperative path: a frame's filtering runs in a tiny_fiber fiber whose
    // filters call CheckPoint() once per row. It is pumped in short slices,
    // back to back, between display ticks (_pumpCooperative), so heavy
    // filtering never blocks the main thread for long and still finishes as
    // soon as the synchronous path would. A frame that arrives while a render
    // is running is rendered next; several such frames collapse into one.
    _requestCooperative(p) {
        this._dirty = false;
        if (this._coopActive) {
            this._coopQueued = true;
            return;
        }
        this._beginCooperative(p);
    }

    _beginCooperative(p) {
        this._coopIndex = p.produce(this._client);
        this._client.beginCooperativeRender(this._coopIndex);
        this._coopActive = true;
        this._coopQueued = false;
        this._schedulePump();
    }

    _schedulePump() {
        if (!this._pumpQueued) {
            this._pumpQueued = true;
            this._pump.post();
        }
    }

    _pumpCooperative() {
        if (!this._coopActive) {
            return; // cancelled: the mode or the source changed
        }
        const client = this._client;
        try {
            client.runCooperativeFor(COOPERATIVE_SLICE_MS);
            if (!client.cooperativeDone()) {
                this._schedulePump(); // let the browser in, then continue
                return;
            }
        } catch (error) {
            // Never leave the preview stuck: render this frame synchronously.
            console.error("cooperative render failed; rendering synchronously", error);
            client.renderPreview(this._coopIndex);
        }
        this._paint(this._coopIndex);
        this._coopActive = false;
        if (this._coopQueued && this._running && this._provider) {
            this._beginCooperative(this._provider);
        }
    }

    _paint(idx) {
        const pixels = this._client.outputFrame(idx);
        this._canvas.draw(pixels, this._client.width(), this._client.height());
    }
}
