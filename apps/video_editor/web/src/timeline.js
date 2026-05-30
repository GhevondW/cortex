// timeline.js
//
// Position scrubber + play/pause for the live editor. The range is a normalized
// fraction (0..1000 integer steps); the controller maps it onto the active
// provider (seconds for video, frames for the procedural sample). Programmatic
// position updates via setPosition() do NOT echo a 'seek' (avoids feedback while
// dragging). Emits:
//   'seek'        detail: fraction in [0,1]
//   'toggle-play' (no detail)

export class TimelineControl extends EventTarget {
    constructor(rangeEl, labelEl, playBtnEl) {
        super();
        this._range = rangeEl;
        this._label = labelEl;
        this._play = playBtnEl;
        this._seeking = false;

        this._range.min = "0";
        this._range.max = "1000";
        this._range.step = "1";
        this._range.value = "0";

        this._range.addEventListener("input", () => {
            this._seeking = true;
            this.dispatchEvent(new CustomEvent("seek", { detail: Number(this._range.value) / 1000 }));
        });
        // Release on change/pointerup so live updates resume.
        this._range.addEventListener("change", () => { this._seeking = false; });
        this._range.addEventListener("pointerup", () => { this._seeking = false; });

        if (this._play) {
            this._play.addEventListener("click", () => this.dispatchEvent(new Event("toggle-play")));
        }
    }

    setPosition({ current, total, unit }) {
        if (!this._seeking && total > 0) {
            this._range.value = String(Math.round((current / total) * 1000));
        }
        if (this._label) {
            this._label.textContent = unit === "s"
                ? `${current.toFixed(1)}s / ${total.toFixed(1)}s`
                : `${current} / ${total}`;
        }
    }

    setPlaying(playing) {
        if (this._play) this._play.textContent = playing ? "❚❚ Pause" : "► Play";
    }

    setEnabled(enabled) {
        this._range.disabled = !enabled;
        if (this._play) this._play.disabled = !enabled;
    }
}
