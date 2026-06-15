// frame_provider.js
//
// A FrameProvider feeds the editor the "current" frame each tick and owns
// playback position. Two implementations share one contract so the live loop
// (playback.js) and the controller (editor_app.js) never branch on source type:
//
//   - VideoProvider:      wraps an <video> playing a local file. produce() draws
//                         the current video frame and copies its pixels into the
//                         engine's single source slot (0).
//   - ProceduralProvider: the zero-file "sample clip". The engine already holds
//                         N procedural frames in WASM, so produce() writes no
//                         pixels — it just advances which frame index the engine
//                         renders, over wall-clock, so the sample appears to play.
//
// Contract used by playback.js / editor_app.js:
//   await provider.open()             -> { width, height }  (working resolution)
//   provider.produce(client)          -> frameIndex         (writes pixels if any)
//   provider.position                 -> { current, total, unit:'s'|'f' }
//   provider.seekFraction(f01)        -> void
//   provider.play() / pause()         -> (async for video)
//   provider.playing                  -> boolean
//   provider.hasVideoFrameCallback    -> boolean
//   provider.requestVideoFrame(cb)    -> schedules one rVFC tick (video only)
//   provider.dispose()                -> release resources
//   provider extends EventTarget; VideoProvider emits 'error'.

const EVEN = (n) => Math.max(2, Math.round(n / 2) * 2);

// Fit (srcW, srcH) inside a longest-side cap, preserving aspect ratio, with even
// dimensions (some decoders/canvas paths prefer even sizes). Never upscales.
export function fitWorkingSize(srcW, srcH, cap) {
    if (srcW <= 0 || srcH <= 0) return { width: EVEN(cap), height: EVEN(cap) };
    const scale = Math.min(1, cap / Math.max(srcW, srcH));
    return { width: EVEN(srcW * scale), height: EVEN(srcH * scale) };
}

export class VideoProvider extends EventTarget {
    constructor(file, { cap = 640 } = {}) {
        super();
        this._file = file;
        this._cap = cap;
        this._url = null;
        this._video = null;
        this._cnv = document.createElement("canvas");
        this._ctx = this._cnv.getContext("2d", { willReadFrequently: true });
        this._w = 0;
        this._h = 0;
    }

    async open() {
        this._url = URL.createObjectURL(this._file);
        const v = document.createElement("video");
        v.src = this._url;
        v.muted = true;        // muted autoplay is always allowed; unmute control optional
        v.loop = true;         // loop the clip so it keeps playing while you edit
        v.playsInline = true;
        v.preload = "auto";
        this._video = v;

        await new Promise((resolve, reject) => {
            const onData = () => { cleanup(); resolve(); };
            const onErr = () => {
                cleanup();
                reject(new Error("Could not decode this video — try an MP4/WebM your browser supports"));
            };
            const cleanup = () => {
                v.removeEventListener("loadeddata", onData);
                v.removeEventListener("error", onErr);
            };
            v.addEventListener("loadeddata", onData, { once: true });
            v.addEventListener("error", onErr, { once: true });
        });

        const size = fitWorkingSize(v.videoWidth, v.videoHeight, this._cap);
        this._w = size.width;
        this._h = size.height;
        this._cnv.width = this._w;
        this._cnv.height = this._h;
        // Surface late decode errors (corrupt mid-stream) without crashing.
        v.addEventListener("error", () => this.dispatchEvent(new Event("error")));
        return { width: this._w, height: this._h };
    }

    produce(client) {
        // Draw the current video frame into the working canvas, then push its
        // RGBA bytes into engine source slot 0.
        this._ctx.drawImage(this._video, 0, 0, this._w, this._h);
        const img = this._ctx.getImageData(0, 0, this._w, this._h);
        client.writeSourceFrame(0, img.data);
        return 0;
    }

    get position() {
        const v = this._video;
        return {
            current: v ? v.currentTime : 0,
            total: v ? (Number.isFinite(v.duration) ? v.duration : 0) : 0,
            unit: "s",
        };
    }

    seekFraction(f) {
        const v = this._video;
        if (v && Number.isFinite(v.duration) && v.duration > 0) {
            v.currentTime = Math.max(0, Math.min(v.duration, f * v.duration));
        }
    }

    async play() {
        if (!this._video) return;
        try {
            await this._video.play();
        } catch {
            // Autoplay rejected despite the user gesture — caller shows a Play button.
        }
    }
    pause() { if (this._video) this._video.pause(); }
    get playing() { return !!this._video && !this._video.paused && !this._video.ended; }

    setMuted(muted) { if (this._video) this._video.muted = muted; }
    get muted() { return !this._video || this._video.muted; }

    get hasVideoFrameCallback() {
        return !!this._video && "requestVideoFrameCallback" in this._video;
    }
    requestVideoFrame(cb) { return this._video.requestVideoFrameCallback(cb); }
    cancelVideoFrame(handle) {
        if (this._video && "cancelVideoFrameCallback" in this._video) {
            this._video.cancelVideoFrameCallback(handle);
        }
    }

    dispose() {
        if (this._video) {
            try {
                this._video.pause();
                this._video.removeAttribute("src");
                this._video.load();
            } catch {
                // best effort
            }
        }
        if (this._url) {
            URL.revokeObjectURL(this._url);
            this._url = null;
        }
        this._video = null;
    }
}

export class ProceduralProvider extends EventTarget {
    // The engine already owns N procedural frames; we just choose which one to
    // render based on elapsed wall-clock so the sample "plays". produce() writes
    // no pixels (they live in WASM) and returns the frame index.
    constructor({ frameCount = 180, fps = 24, width = 320, height = 240 } = {}) {
        super();
        this._n = Math.max(1, frameCount);
        this._fps = fps;
        this._w = width;
        this._h = height;
        this._epoch = null;     // seconds at which "playback" started
        this._paused = true;
        this._pausedElapsed = 0;
    }

    async open() {
        this._epoch = null;
        this._paused = false;
        this._pausedElapsed = 0;
        return { width: this._w, height: this._h };
    }

    _now() {
        return (typeof performance !== "undefined" ? performance.now() : 0) / 1000;
    }

    _elapsed() {
        if (this._paused) return this._pausedElapsed;
        if (this._epoch === null) this._epoch = this._now();
        return this._now() - this._epoch;
    }

    produce(_client) {
        const frames = Math.floor(this._elapsed() * this._fps);
        return ((frames % this._n) + this._n) % this._n;
    }

    get position() {
        return { current: this.produce(null), total: this._n, unit: "f" };
    }

    seekFraction(f) {
        const seconds = Math.max(0, Math.min(1, f)) * (this._n / this._fps);
        if (this._paused) {
            this._pausedElapsed = seconds;
        } else {
            this._epoch = this._now() - seconds;
        }
    }

    async play() {
        if (this._paused) {
            this._epoch = this._now() - this._pausedElapsed;
            this._paused = false;
        }
    }
    pause() {
        if (!this._paused) {
            this._pausedElapsed = this._elapsed();
            this._paused = true;
        }
    }
    get playing() { return !this._paused; }

    get hasVideoFrameCallback() { return false; }
    requestVideoFrame() {}
    cancelVideoFrame() {}
    dispose() {}
}
