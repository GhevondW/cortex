// Timeline scrubber: a range input + label showing current frame index.
// Emits 'seek' events when the user drags; programmatic updates via
// .setFrame() do NOT fire 'seek' (to avoid feedback loops with the canvas).

export class TimelineControl extends EventTarget {
    constructor(inputEl, labelEl) {
        super();
        this._input = inputEl;
        this._label = labelEl;
        this._total = 0;
        this._input.addEventListener("input", () => {
            const idx = Number(this._input.value);
            this._updateLabel(idx);
            this.dispatchEvent(new CustomEvent("seek", { detail: idx }));
        });
    }

    setFrameCount(total) {
        this._total = total;
        this._input.min = 0;
        this._input.max = Math.max(0, total - 1);
        if (Number(this._input.value) >= total) {
            this._input.value = String(total - 1);
        }
        this._updateLabel(Number(this._input.value));
    }

    setFrame(idx) {
        if (idx < 0 || idx >= this._total) return;
        this._input.value = String(idx);
        this._updateLabel(idx);
    }

    currentFrame() {
        return Number(this._input.value);
    }

    setEnabled(enabled) {
        this._input.disabled = !enabled;
    }

    _updateLabel(idx) {
        if (this._label) {
            this._label.textContent = `${idx + 1} / ${this._total}`;
        }
    }
}
