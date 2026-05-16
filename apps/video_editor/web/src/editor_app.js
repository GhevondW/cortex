// Controller class. Subscribes to UI events, drives the EditorClient, owns
// the canvas-refresh and scheduler-step rAF loops. Holds the small amount
// of mutable state (current frame, AB-mode flag, run state) the demo needs.

export class EditorApp {
    constructor({ client, canvas, timeline, filters, apply, progress, source, uploader }) {
        this._client = client;
        this._canvas = canvas;
        this._timeline = timeline;
        this._filters = filters;
        this._apply = apply;
        this._progress = progress;
        this._source = source;
        this._uploader = uploader;

        this._currentFrame = 0;
        this._showOutput = true;
        this._running = false;
        this._uploading = false;
        this._stepRafId = 0;
        this._defaults = null;
    }

    init(width, height, frameCount) {
        if (!this._client.init(width, height, frameCount)) {
            throw new Error("editor_init returned false");
        }
        this._defaults = { width, height, frameCount };
        this._canvas.resize(width, height);
        this._timeline.setFrameCount(frameCount);
        this._installBridgeCallbacks();
        this._wireEvents();
        // Initial preview at frame 0.
        this._applyFilterValues();
        this._client.renderPreview(0);
        this._redraw();
        this._progress.setStatus("Ready");
        if (this._source) {
            this._source.setInfo(`Procedural (${frameCount} frames)`);
        }
    }

    _wireEvents() {
        this._timeline.addEventListener("seek", (e) => {
            this._currentFrame = e.detail;
            // Skip live re-render while a cooperative run is in progress —
            // its rAF loop will pick up the new frame anyway, and we don't
            // want to fight it for the filter chain.
            if (!this._running) {
                this._client.renderPreview(this._currentFrame);
            }
            this._redraw();
        });

        this._filters.addEventListener("change", () => {
            this._applyFilterValues();
            if (!this._running) {
                this._client.renderPreview(this._currentFrame);
                this._redraw();
            }
        });

        this._apply.addEventListener("apply-cooperative", () => this._startCooperative());
        this._apply.addEventListener("apply-blocking", () => this._runBlocking());
        this._apply.addEventListener("cancel", () => {
            if (this._uploading && this._uploader) {
                this._uploader.cancel();
            } else {
                this._client.cancel();
            }
        });
        this._apply.addEventListener("toggle-ab", () => {
            this._showOutput = !this._showOutput;
            this._redraw();
            this._progress.setStatus(this._showOutput ? "Showing: edited" : "Showing: source");
        });

        if (this._source) {
            this._source.addEventListener("upload-selected", (e) => {
                this._loadUploadedVideo(e.detail);
            });
            this._source.addEventListener("reset-procedural", () => {
                this._resetToProcedural();
            });
        }
    }

    _applyFilterValues() {
        const v = this._filters.values();
        this._client.setBrightness(v.brightness);
        this._client.setContrast(v.contrast);
        this._client.setSaturation(v.saturation);
        this._client.setBlurRadius(v.blur);
    }

    _installBridgeCallbacks() {
        window.onApplyProgress = (pct) => {
            this._progress.set(pct);
        };
        window.onApplyComplete = () => {
            this._running = false;
            this._apply.setRunning(false);
            this._progress.set(1);
            this._progress.setStatus("Apply complete");
            this._client.renderPreview(this._currentFrame);
            this._redraw();
        };
        window.onApplyCancelled = () => {
            this._running = false;
            this._apply.setRunning(false);
            this._progress.setStatus("Cancelled");
        };
    }

    _startCooperative() {
        if (this._running) return;
        this._applyFilterValues();
        this._progress.set(0);
        this._progress.setStatus("Applying (cooperative)…");
        this._running = true;
        this._apply.setRunning(true);
        this._client.startApplyCooperative();
        this._driveSteps();
    }

    _runBlocking() {
        if (this._running) return;
        this._applyFilterValues();
        this._progress.set(0);
        this._progress.setStatus("Applying (blocking)…");
        this._running = true;
        this._apply.setRunning(true);
        // Synchronous call — the UI will be frozen until it returns. That's
        // the whole point of this code path; the cooperative button is the
        // foil. Defer one tick so the status label has a chance to paint.
        window.setTimeout(() => {
            this._client.runApplyBlocking();
            // onApplyComplete will reset state from inside the bridge.
        }, 16);
    }

    _driveSteps() {
        const tick = () => {
            if (!this._running) return;
            const more = this._client.step();
            // Refresh the displayed frame as workers finish frames.
            this._client.renderPreview(this._currentFrame);
            this._redraw();
            if (more) {
                this._stepRafId = window.requestAnimationFrame(tick);
            }
        };
        this._stepRafId = window.requestAnimationFrame(tick);
    }

    _redraw() {
        const pixels = this._showOutput
            ? this._client.outputFrame(this._currentFrame)
            : this._client.sourceFrame(this._currentFrame);
        this._canvas.draw(pixels, this._client.width(), this._client.height());
    }

    async _loadUploadedVideo(file) {
        if (this._uploading) return;
        if (this._running) {
            // Stop any apply pass before mutating the source out from under it.
            this._client.cancel();
        }
        this._uploading = true;
        this._apply.setUploading(true);
        this._filters.setEnabled(false);
        this._source.setEnabled(false);
        this._progress.set(0);
        this._progress.setStatus("Loading video metadata…");

        let plan;
        try {
            plan = await this._uploader.prepare(file);
        } catch (err) {
            this._finishUpload(`Upload failed: ${err.message}`);
            return;
        }

        if (!this._client.resetUploaded(plan.width, plan.height, plan.frameCount)) {
            this._finishUpload("Editor refused upload reset");
            return;
        }

        this._canvas.resize(plan.width, plan.height);
        this._timeline.setFrameCount(plan.frameCount);
        this._currentFrame = 0;
        this._timeline.setFrame(0);

        const onProgress = (e) => {
            const { current, total } = e.detail;
            this._progress.set(current / total);
            this._progress.setStatus(`Decoding ${current} / ${total} frames…`);
        };
        this._uploader.addEventListener("progress", onProgress);

        try {
            await this._uploader.extract((idx, data) => {
                this._client.writeSourceFrame(idx, data);
            });
        } catch (err) {
            this._uploader.removeEventListener("progress", onProgress);
            const msg = err.message === "cancelled"
                ? "Upload cancelled — restoring procedural source"
                : `Upload failed: ${err.message}`;
            this._fallbackToProcedural();
            this._finishUpload(msg);
            return;
        }

        this._uploader.removeEventListener("progress", onProgress);
        this._applyFilterValues();
        this._client.renderPreview(0);
        this._redraw();
        this._source.setInfo(`Uploaded video — ${plan.frameCount} frames @ ${plan.width}×${plan.height}`);
        this._progress.set(1);
        this._finishUpload(`Loaded ${plan.frameCount} frames`);
    }

    _finishUpload(statusText) {
        this._uploading = false;
        this._apply.setUploading(false);
        this._filters.setEnabled(true);
        this._source.setEnabled(true);
        this._progress.setStatus(statusText);
    }

    _resetToProcedural() {
        if (this._uploading || this._running) return;
        const { width, height, frameCount } = this._defaults;
        if (!this._client.resetProcedural(width, height, frameCount)) {
            this._progress.setStatus("Editor refused procedural reset");
            return;
        }
        this._canvas.resize(width, height);
        this._timeline.setFrameCount(frameCount);
        this._currentFrame = 0;
        this._timeline.setFrame(0);
        this._applyFilterValues();
        this._client.renderPreview(0);
        this._redraw();
        this._source.setInfo(`Procedural (${frameCount} frames)`);
        this._progress.set(0);
        this._progress.setStatus("Procedural source restored");
    }

    _fallbackToProcedural() {
        const { width, height, frameCount } = this._defaults;
        if (this._client.resetProcedural(width, height, frameCount)) {
            this._canvas.resize(width, height);
            this._timeline.setFrameCount(frameCount);
            this._currentFrame = 0;
            this._timeline.setFrame(0);
            this._client.renderPreview(0);
            this._redraw();
            this._source.setInfo(`Procedural (${frameCount} frames)`);
        }
    }
}
