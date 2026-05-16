// VideoUploader: decodes a user-supplied video file into a sequence of RGBA
// frames by driving an HTMLVideoElement through `currentTime` seeks and
// capturing each frame via an offscreen <canvas>. Frames are downscaled to
// the editor's working size and the total count is capped — both to keep
// WASM-side memory bounded.
//
// Two-phase API:
//   1. await uploader.prepare(file)  -> { width, height, frameCount, duration }
//      Returns metadata up-front so the caller can re-init the engine before
//      the (potentially slow) per-frame extraction begins.
//   2. await uploader.extract(onFrame) — yields each decoded frame via the
//      callback as `(idx, Uint8ClampedArray)`. Caller writes the bytes
//      directly into the WASM heap; uploader holds at most one frame.
//
// Emits a 'progress' event after every frame; cancel() makes the next yield
// abort the extraction with an Error('cancelled').

const DEFAULT_OPTS = {
    targetWidth: 320,
    targetHeight: 240,
    maxFrames: 120,
    targetFps: 24,
};

export class VideoUploader extends EventTarget {
    constructor(opts = {}) {
        super();
        this._opts = { ...DEFAULT_OPTS, ...opts };
        this._cancelled = false;
        this._video = null;
        this._objectUrl = null;
        this._plan = null;
    }

    cancel() {
        this._cancelled = true;
    }

    async prepare(file) {
        this._cleanup();
        this._cancelled = false;

        const url = URL.createObjectURL(file);
        this._objectUrl = url;

        const video = document.createElement("video");
        video.src = url;
        video.muted = true;
        video.playsInline = true;
        video.preload = "auto";
        // Required on some browsers for the first seeked frame to actually paint.
        video.crossOrigin = "anonymous";
        this._video = video;

        try {
            await this._waitForEvent(video, "loadedmetadata");
        } catch (err) {
            this._cleanup();
            throw new Error(`Failed to load video metadata: ${err.message}`);
        }

        const duration = video.duration;
        if (!Number.isFinite(duration) || duration <= 0) {
            this._cleanup();
            throw new Error(`Invalid video duration: ${duration}`);
        }

        // loadeddata fires when the first frame has been decoded, which keeps
        // the first canvas.drawImage from producing a blank frame.
        try {
            await this._waitForEvent(video, "loadeddata");
        } catch {
            // Non-fatal — proceed; the first seeked event still resolves.
        }

        const { targetWidth, targetHeight, maxFrames, targetFps } = this._opts;
        const desired = Math.max(1, Math.floor(duration * targetFps));
        const frameCount = Math.min(maxFrames, desired);
        const step = duration / frameCount;

        this._plan = {
            width: targetWidth,
            height: targetHeight,
            frameCount,
            duration,
            step,
        };
        return { ...this._plan };
    }

    async extract(onFrame) {
        if (!this._video || !this._plan) {
            throw new Error("VideoUploader.extract called before prepare()");
        }
        const video = this._video;
        const { width, height, frameCount, duration, step } = this._plan;

        const canvas = document.createElement("canvas");
        canvas.width = width;
        canvas.height = height;
        const ctx = canvas.getContext("2d", { willReadFrequently: true });

        try {
            for (let i = 0; i < frameCount; ++i) {
                if (this._cancelled) {
                    throw new Error("cancelled");
                }
                // Sample the middle of each evenly-spaced slot to avoid the
                // exact 0.0/duration boundaries that some decoders return as
                // black/identical frames.
                const t = Math.min(duration - 1e-3, step * (i + 0.5));
                await this._seek(video, t);
                ctx.drawImage(video, 0, 0, width, height);
                const imgData = ctx.getImageData(0, 0, width, height);
                onFrame(i, imgData.data);

                this.dispatchEvent(new CustomEvent("progress", {
                    detail: { current: i + 1, total: frameCount },
                }));
            }
        } finally {
            this._cleanup();
        }
    }

    _seek(video, time) {
        return new Promise((resolve, reject) => {
            const onSeeked = () => {
                cleanup();
                resolve();
            };
            const onError = () => {
                cleanup();
                reject(new Error(`Seek to ${time.toFixed(3)}s failed`));
            };
            const cleanup = () => {
                video.removeEventListener("seeked", onSeeked);
                video.removeEventListener("error", onError);
            };
            video.addEventListener("seeked", onSeeked);
            video.addEventListener("error", onError);
            // Setting currentTime triggers the seek; the 'seeked' event fires
            // when decode of the target frame completes.
            video.currentTime = time;
        });
    }

    _waitForEvent(el, event) {
        return new Promise((resolve, reject) => {
            const onOk = () => {
                cleanup();
                resolve();
            };
            const onErr = () => {
                cleanup();
                reject(new Error(`${event} fired error`));
            };
            const cleanup = () => {
                el.removeEventListener(event, onOk);
                el.removeEventListener("error", onErr);
            };
            el.addEventListener(event, onOk, { once: true });
            el.addEventListener("error", onErr, { once: true });
        });
    }

    _cleanup() {
        if (this._video) {
            try {
                this._video.removeAttribute("src");
                this._video.load();
            } catch {
                // Best-effort; ignore.
            }
            this._video = null;
        }
        if (this._objectUrl) {
            URL.revokeObjectURL(this._objectUrl);
            this._objectUrl = null;
        }
        this._plan = null;
    }
}
