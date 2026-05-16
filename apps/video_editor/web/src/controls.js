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
        this._running = false;
        this._uploading = false;
        buttons.cooperative.addEventListener("click", () =>
            this.dispatchEvent(new Event("apply-cooperative")));
        buttons.blocking.addEventListener("click", () =>
            this.dispatchEvent(new Event("apply-blocking")));
        buttons.cancel.addEventListener("click", () =>
            this.dispatchEvent(new Event("cancel")));
        buttons.ab.addEventListener("click", () =>
            this.dispatchEvent(new Event("toggle-ab")));
        this._refresh();
    }

    setRunning(running) {
        this._running = running;
        this._refresh();
    }

    setUploading(uploading) {
        this._uploading = uploading;
        this._refresh();
    }

    _refresh() {
        const busy = this._running || this._uploading;
        this._btn.cooperative.disabled = busy;
        this._btn.blocking.disabled = busy;
        this._btn.cancel.disabled = !busy;
    }
}

// SourceControls — wraps the file input + reset-to-procedural button.
// Emits 'upload-selected' (detail: File) when the user picks a video file
// and 'reset-procedural' when the reset button is clicked.
export class SourceControls extends EventTarget {
    constructor({ upload, reset, info }) {
        super();
        this._inputs = { upload, reset };
        this._info = info;
        // The file input is visually hidden; the label is what users click.
        // Disabling the input alone leaves the label looking active, so we
        // also toggle a CSS class on the associated label.
        this._uploadLabel = upload.id
            ? document.querySelector(`label[for="${upload.id}"]`)
            : null;

        upload.addEventListener("change", (e) => {
            const file = e.target.files && e.target.files[0];
            if (!file) return;
            this.dispatchEvent(new CustomEvent("upload-selected", { detail: file }));
            // Reset value so picking the same file twice still fires.
            e.target.value = "";
        });
        reset.addEventListener("click", () =>
            this.dispatchEvent(new Event("reset-procedural")));
    }

    setEnabled(enabled) {
        this._inputs.upload.disabled = !enabled;
        this._inputs.reset.disabled = !enabled;
        if (this._uploadLabel) {
            this._uploadLabel.classList.toggle("disabled", !enabled);
        }
    }

    setInfo(text) {
        if (this._info) {
            this._info.textContent = text;
        }
    }
}
