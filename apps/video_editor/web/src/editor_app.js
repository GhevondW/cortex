// Controller for the live editor. Owns the active FrameProvider, drives the
// Playback loop, and keeps filter/engine state. It wires UI events to provider
// + client calls; it holds no pixel data of its own.

import { VideoProvider, ProceduralProvider } from "./frame_provider.js";

export class EditorApp {
    constructor({ client, canvas, timeline, filters, engine, source, progress, playback }) {
        this._client = client;
        this._canvas = canvas;
        this._timeline = timeline;
        this._filters = filters;
        this._engine = engine;
        this._source = source;
        this._progress = progress;
        this._playback = playback;

        this._provider = null;
        this._sampleOpts = null;
        this._working = { cap: 640 };
        this._busy = false; // true while opening a file
    }

    init(width, height, frameCount) {
        if (!this._client.init(width, height, frameCount)) {
            throw new Error("editor_init returned false");
        }
        this._sampleOpts = { width, height, frameCount, fps: 24 };
        this._canvas.resize(width, height);
        this._applyFilterValues();
        this._wireEvents();

        // Start on the animated procedural sample so the page is alive on load.
        this._provider = new ProceduralProvider(this._sampleOpts);
        this._provider.open();
        this._playback.setProvider(this._provider);
        this._playback.start();
        this._provider.play();
        this._timeline.setPlaying(true);
        this._source.setInfo(`Sample clip — procedural (${frameCount} frames)`);
        this._progress.setStatus("Ready — playing sample. Open a video to edit your own.");
    }

    _wireEvents() {
        this._filters.addEventListener("change", () => {
            this._applyFilterValues();
            this._playback.markDirty();
        });

        this._engine.addEventListener("toggle-cooperative", (e) => this._setCooperative(e.detail));

        this._timeline.addEventListener("seek", (e) => {
            if (this._provider) this._provider.seekFraction(e.detail);
            this._playback.markDirty();
        });
        this._timeline.addEventListener("toggle-play", () => this._togglePlay());

        this._source.addEventListener("open-selected", (e) => this._openVideo(e.detail));
        this._source.addEventListener("use-sample", () => this._useSample());

        // Keep the play/pause label honest with the real provider state.
        this._playback.setOnPosition((pos) => {
            this._timeline.setPosition(pos);
            if (this._provider) this._timeline.setPlaying(this._provider.playing);
        });
    }

    _applyFilterValues() {
        const v = this._filters.values();
        this._client.setBrightness(v.brightness);
        this._client.setContrast(v.contrast);
        this._client.setSaturation(v.saturation);
        this._client.setBlurRadius(v.blur);
    }

    _setCooperative(on) {
        if (on && !this._client.supportsCooperative()) {
            this._engine.set(false);
            this._playback.setCooperative(false);
            this._progress.setStatus("Cooperative engine isn't available in this build.");
            return;
        }
        this._playback.setCooperative(on);
        this._progress.setStatus(on
            ? "Cooperative engine on — heavy filters won't freeze the page."
            : "Cooperative engine off (synchronous).");
    }

    _togglePlay() {
        if (!this._provider) return;
        if (this._provider.playing) {
            this._provider.pause();
            this._timeline.setPlaying(false);
        } else {
            this._provider.play();
            this._timeline.setPlaying(true);
        }
    }

    async _openVideo(file) {
        if (this._busy) return;
        this._busy = true;
        this._filters.setEnabled(false);
        this._source.setEnabled(false);
        this._progress.set(0);
        this._progress.setStatus(`Opening "${file.name}"…`);

        const provider = new VideoProvider(file, { cap: this._working.cap });
        try {
            const plan = await provider.open();

            if (!this._client.resetUploaded(plan.width, plan.height, 1)) {
                provider.dispose();
                this._progress.setStatus("Engine refused the uploaded source.");
                return;
            }

            this._swapProvider(provider);
            this._canvas.resize(plan.width, plan.height);
            this._applyFilterValues();
            provider.play();
            this._timeline.setPlaying(true);
            this._source.setInfo(`Opened "${file.name}" — ${plan.width}×${plan.height}`);
            this._progress.set(1);
            this._progress.setStatus("Editing live — move the sliders while it plays.");
        } catch (err) {
            provider.dispose();
            this._progress.setStatus(err.message || "Could not open this video.");
        } finally {
            // Always re-enable the UI and clear _busy, even if opening threw, so the
            // editor can never get wedged with its controls disabled.
            this._finishOpen();
        }
    }

    _useSample() {
        if (this._busy) return;
        const { width, height, frameCount } = this._sampleOpts;
        if (!this._client.resetProcedural(width, height, frameCount)) {
            this._progress.setStatus("Engine refused the procedural reset.");
            return;
        }
        const provider = new ProceduralProvider(this._sampleOpts);
        provider.open();
        this._swapProvider(provider);
        this._canvas.resize(width, height);
        this._applyFilterValues();
        provider.play();
        this._timeline.setPlaying(true);
        this._source.setInfo(`Sample clip — procedural (${frameCount} frames)`);
        this._progress.setStatus("Playing sample.");
    }

    _swapProvider(next) {
        const prev = this._provider;
        this._provider = next;
        this._playback.setProvider(next);
        this._playback.start();
        if (prev && prev !== next) prev.dispose();
    }

    _finishOpen() {
        this._busy = false;
        this._filters.setEnabled(true);
        this._source.setEnabled(true);
    }
}
