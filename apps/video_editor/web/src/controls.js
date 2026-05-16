// FilterControls — four range sliders. Emits 'change' with {key, value}.
// ApplyControls — three buttons + AB toggle. Emits 'apply-cooperative',
//   'apply-blocking', 'cancel', 'toggle-ab' events.

export class FilterControls extends EventTarget {
    constructor(sliders) {
        super();
        // sliders: { brightness, contrast, saturation, blur }
        this._inputs = sliders;
        for (const [key, el] of Object.entries(sliders)) {
            el.addEventListener("input", () => {
                this.dispatchEvent(new CustomEvent("change", {
                    detail: { key, value: Number(el.value) },
                }));
            });
        }
    }

    values() {
        return {
            brightness: Number(this._inputs.brightness.value),
            contrast:   Number(this._inputs.contrast.value),
            saturation: Number(this._inputs.saturation.value),
            blur:       Number(this._inputs.blur.value),
        };
    }

    setEnabled(enabled) {
        for (const el of Object.values(this._inputs)) {
            el.disabled = !enabled;
        }
    }
}

export class ApplyControls extends EventTarget {
    constructor(buttons) {
        super();
        // buttons: { cooperative, blocking, cancel, ab }
        this._btn = buttons;
        buttons.cooperative.addEventListener("click", () =>
            this.dispatchEvent(new Event("apply-cooperative")));
        buttons.blocking.addEventListener("click", () =>
            this.dispatchEvent(new Event("apply-blocking")));
        buttons.cancel.addEventListener("click", () =>
            this.dispatchEvent(new Event("cancel")));
        buttons.ab.addEventListener("click", () =>
            this.dispatchEvent(new Event("toggle-ab")));
    }

    setRunning(running) {
        this._btn.cooperative.disabled = running;
        this._btn.blocking.disabled = running;
        this._btn.cancel.disabled = !running;
    }
}
