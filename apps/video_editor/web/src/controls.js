// Controls for the live editor.
//
//   FilterControls — four range sliders. Emits 'change' with {key, value}.
//   EngineControls — the "Cortex cooperative engine" checkbox. Emits
//                    'toggle-cooperative' with a boolean.
//   SourceControls — "Open video…" file input + "Use sample clip" button.
//                    Emits 'open-selected' (detail: File) and 'use-sample'.

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

export class EngineControls extends EventTarget {
    constructor(checkboxEl) {
        super();
        this._chk = checkboxEl;
        if (this._chk) {
            this._chk.addEventListener("change", () => {
                this.dispatchEvent(new CustomEvent("toggle-cooperative", { detail: this._chk.checked }));
            });
        }
    }

    get cooperative() { return !!(this._chk && this._chk.checked); }
    set(on) { if (this._chk) this._chk.checked = !!on; }
}

export class SourceControls extends EventTarget {
    constructor({ open, sample, info }) {
        super();
        this._inputs = { open, sample };
        this._info = info;
        // The file input is visually hidden; users click its label. Disabling
        // the input alone leaves the label looking active, so toggle a class too.
        this._openLabel = open.id
            ? document.querySelector(`label[for="${open.id}"]`)
            : null;

        open.addEventListener("change", (e) => {
            const file = e.target.files && e.target.files[0];
            if (!file) return;
            this.dispatchEvent(new CustomEvent("open-selected", { detail: file }));
            // Reset value so picking the same file twice still fires.
            e.target.value = "";
        });
        sample.addEventListener("click", () => this.dispatchEvent(new Event("use-sample")));
    }

    setEnabled(enabled) {
        this._inputs.open.disabled = !enabled;
        this._inputs.sample.disabled = !enabled;
        if (this._openLabel) {
            this._openLabel.classList.toggle("disabled", !enabled);
        }
    }

    setInfo(text) {
        if (this._info) this._info.textContent = text;
    }
}
