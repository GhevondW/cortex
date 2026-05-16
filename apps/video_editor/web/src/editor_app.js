// Controller class. Subscribes to UI events, drives the EditorClient, owns
// the canvas-refresh and scheduler-step rAF loops. Holds the small amount
// of mutable state (current frame, AB-mode flag, run state) the demo needs.

export class EditorApp {
    constructor({ client, canvas, timeline, filters, apply, progress }) {
        this._client = client;
        this._canvas = canvas;
        this._timeline = timeline;
        this._filters = filters;
        this._apply = apply;
        this._progress = progress;

        this._currentFrame = 0;
        this._showOutput = true;
        this._running = false;
        this._stepRafId = 0;
    }

    init(width, height, frameCount) {
        if (!this._client.init(width, height, frameCount)) {
            throw new Error("editor_init returned false");
        }
        this._canvas.resize(width, height);
        this._timeline.setFrameCount(frameCount);
        this._installBridgeCallbacks();
        this._wireEvents();
        // Initial preview at frame 0.
        this._applyFilterValues();
        this._client.renderPreview(0);
        this._redraw();
        this._progress.setStatus("Ready");
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
        this._apply.addEventListener("cancel", () => this._client.cancel());
        this._apply.addEventListener("toggle-ab", () => {
            this._showOutput = !this._showOutput;
            this._redraw();
            this._progress.setStatus(this._showOutput ? "Showing: edited" : "Showing: source");
        });
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
}
