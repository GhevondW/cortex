// Renders progress 0..1 to a fixed-width bar element and a percent label.

export class ProgressView {
    constructor(barEl, labelEl, statusEl) {
        this._bar = barEl;
        this._label = labelEl;
        this._status = statusEl;
        this.set(0);
        this.setStatus("Idle");
    }

    set(pct) {
        const clamped = Math.max(0, Math.min(1, pct));
        this._bar.style.width = `${(clamped * 100).toFixed(1)}%`;
        if (this._label) {
            this._label.textContent = `${(clamped * 100).toFixed(0)}%`;
        }
    }

    setStatus(text) {
        if (this._status) {
            this._status.textContent = text;
        }
    }
}
