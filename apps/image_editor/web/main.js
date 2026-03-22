const dom = {
    canvasContainer: document.getElementById("canvasContainer"),
    previewCanvas: document.getElementById("previewCanvas"),
    emptyMsg: document.getElementById("emptyMsg"),
    fileInput: document.getElementById("fileInput"),
    resetBtn: document.getElementById("resetBtn"),
    grayscaleSlider: document.getElementById("grayscaleSlider"),
    blurSlider: document.getElementById("blurSlider"),
    grayscaleValue: document.getElementById("grayscaleValue"),
    blurValue: document.getElementById("blurValue"),
    statusText: document.getElementById("statusText"),
    imageMeta: document.getElementById("imageMeta"),
    outputSummary: document.getElementById("outputSummary"),
    statusImage: document.getElementById("statusImage"),
    statusMode: document.getElementById("statusMode"),
};

const displayCtx = dom.previewCanvas.getContext("2d");
const ASSET_VERSION = "20260322h";

const state = {
    wasm: null,
    pumping: false,
    pumpQueued: false,
    latestRevision: -1,
    imageLoaded: false,
    sourceCanvas: null,
    processedCanvas: null,
    supportsCopyOutput: false,
};

function setStatus(text) {
    dom.statusText.textContent = text;
}

function setFilterControlsEnabled(enabled) {
    dom.grayscaleSlider.disabled = !enabled;
    dom.blurSlider.disabled = !enabled;
    dom.resetBtn.disabled = !enabled;
}

function updateSliderLabels() {
    dom.grayscaleValue.textContent = `${dom.grayscaleSlider.value}%`;
    dom.blurValue.textContent = `${dom.blurSlider.value} px`;
}

function updateSummaryText() {
    if (!state.sourceCanvas) {
        dom.outputSummary.textContent = "No image loaded";
        dom.statusImage.textContent = "none";
        dom.statusMode.textContent = "Source";
        return;
    }

    dom.statusImage.textContent = `${state.sourceCanvas.width} x ${state.sourceCanvas.height}`;
    dom.outputSummary.textContent = state.imageLoaded
        ? `grayscale ${dom.grayscaleSlider.value}% | blur ${dom.blurSlider.value}px`
        : "Source preview loaded, engine upload pending";
}

function runtimeStatusLabel(code) {
    switch (code) {
        case 0:
            return state.imageLoaded ? "Preparing image" : "Waiting for image";
        case 1:
            return "Idle";
        case 2:
            return "Processing";
        case 3:
            return "Preview ready";
        case 4:
            return "Error";
        default:
            return "Unknown";
    }
}

function loadWasmRuntime() {
    return new Promise((resolve, reject) => {
        const runtimeUrl = `./image_editor.js?v=${ASSET_VERSION}`;
        const moduleObj = {};
        moduleObj.locateFile = (path, scriptDirectory) => new URL(path, scriptDirectory || runtimeUrl).href;
        moduleObj.onAbort = (message) => reject(new Error(message || "WASM aborted"));
        moduleObj.onRuntimeInitialized = () => resolve(moduleObj);
        window.Module = moduleObj;

        const script = document.createElement("script");
        script.src = runtimeUrl;
        script.async = true;
        script.onerror = () => reject(new Error(`Failed to load ${runtimeUrl}`));
        document.head.appendChild(script);
    });
}

function call(name, returnType, argTypes = [], args = []) {
    return state.wasm.ccall(name, returnType, argTypes, args);
}

function getHeapU8() {
    return state.wasm?.HEAPU8 || globalThis.HEAPU8 || null;
}

function needsPump() {
    return call("editor_needs_pump", "number") === 1;
}

function resizeDisplayCanvas() {
    const width = Math.max(1, Math.floor(dom.canvasContainer.clientWidth));
    const height = Math.max(1, Math.floor(dom.canvasContainer.clientHeight));

    if (dom.previewCanvas.width !== width || dom.previewCanvas.height !== height) {
        dom.previewCanvas.width = width;
        dom.previewCanvas.height = height;
    }
}

function drawSurface(surfaceCanvas) {
    resizeDisplayCanvas();

    displayCtx.clearRect(0, 0, dom.previewCanvas.width, dom.previewCanvas.height);
    displayCtx.fillStyle = "#141821";
    displayCtx.fillRect(0, 0, dom.previewCanvas.width, dom.previewCanvas.height);

    if (!surfaceCanvas) {
        dom.emptyMsg.style.display = "flex";
        return;
    }

    dom.emptyMsg.style.display = "none";

    const sourceWidth = surfaceCanvas.width;
    const sourceHeight = surfaceCanvas.height;
    const scale = Math.min(dom.previewCanvas.width / sourceWidth, dom.previewCanvas.height / sourceHeight);
    const drawWidth = Math.max(1, Math.floor(sourceWidth * scale));
    const drawHeight = Math.max(1, Math.floor(sourceHeight * scale));
    const offsetX = Math.floor((dom.previewCanvas.width - drawWidth) / 2);
    const offsetY = Math.floor((dom.previewCanvas.height - drawHeight) / 2);

    displayCtx.imageSmoothingEnabled = true;
    displayCtx.drawImage(surfaceCanvas, offsetX, offsetY, drawWidth, drawHeight);
}

function renderPreview() {
    drawSurface(state.processedCanvas || state.sourceCanvas);
}

function refreshStatus() {
    if (!state.wasm) {
        return;
    }

    const statusCode = call("editor_get_status", "number");
    const label = runtimeStatusLabel(statusCode);

    if (statusCode === 4) {
        const error = call("editor_get_error_message", "string") || "Unknown processing error";
        setStatus(`${label}: ${error}`);
        dom.statusMode.textContent = "Error";
        return;
    }

    setStatus(label);
    if (statusCode === 2) {
        dom.statusMode.textContent = "Processing";
    } else if (state.processedCanvas) {
        dom.statusMode.textContent = "Processed";
    } else {
        dom.statusMode.textContent = "Source";
    }
}

function refreshPreviewIfNeeded() {
    if (!state.wasm) {
        return;
    }

    const revision = call("editor_get_output_revision", "number");
    if (revision === state.latestRevision) {
        return;
    }

    const width = call("editor_get_output_width", "number");
    const height = call("editor_get_output_height", "number");
    const size = call("editor_get_output_size", "number");
    if (!width || !height || !size) {
        return;
    }

    const heapU8 = getHeapU8();
    if (!heapU8) {
        throw new Error("HEAPU8 is unavailable on the WASM runtime");
    }

    let pixelCopy;
    if (state.supportsCopyOutput) {
        const ptr = state.wasm._malloc(size);
        try {
            const written = call("editor_copy_output_rgba", "number", ["number", "number"], [ptr, size]);
            if (written !== size) {
                throw new Error(`editor_copy_output_rgba wrote ${written} bytes, expected ${size}`);
            }
            pixelCopy = new Uint8ClampedArray(heapU8.slice(ptr, ptr + size));
        } finally {
            state.wasm._free(ptr);
        }
    } else {
        const ptr = call("editor_get_output_ptr", "number");
        if (!ptr) {
            return;
        }
        pixelCopy = new Uint8ClampedArray(heapU8.slice(ptr, ptr + size));
    }

    const imageData = new ImageData(pixelCopy, width, height);
    const bufferCanvas = document.createElement("canvas");
    bufferCanvas.width = width;
    bufferCanvas.height = height;
    bufferCanvas.getContext("2d").putImageData(imageData, 0, 0);

    state.processedCanvas = bufferCanvas;
    state.latestRevision = revision;
    renderPreview();
}

function pumpOnce() {
    if (!state.wasm || state.pumping) {
        return;
    }

    state.pumping = true;
    try {
        call("editor_pump", "number", ["number"], [16]);
        refreshPreviewIfNeeded();
        refreshStatus();
    } catch (error) {
        setStatus(`Runtime error: ${error.message}`);
        dom.statusMode.textContent = "Error";
    } finally {
        state.pumping = false;
    }

    if (needsPump()) {
        ensurePumpLoop();
    }
}

function ensurePumpLoop() {
    if (state.pumpQueued || !state.wasm) {
        return;
    }

    state.pumpQueued = true;
    requestAnimationFrame(() => {
        state.pumpQueued = false;
        pumpOnce();
    });
}

function queueFilterUpdate() {
    if (!state.wasm || !state.imageLoaded) {
        return;
    }

    updateSummaryText();
    setStatus("Processing");
    dom.statusMode.textContent = "Processing";
    ensurePumpLoop();
}

async function uploadImage(file) {
    const bitmap = await createImageBitmap(file);
    const sourceCanvas = document.createElement("canvas");
    sourceCanvas.width = bitmap.width;
    sourceCanvas.height = bitmap.height;

    const sourceCtx = sourceCanvas.getContext("2d", { willReadFrequently: true });
    sourceCtx.drawImage(bitmap, 0, 0);
    const imageData = sourceCtx.getImageData(0, 0, bitmap.width, bitmap.height);
    const uploadBytes = new Uint8Array(imageData.data.buffer, imageData.data.byteOffset, imageData.data.byteLength);

    state.sourceCanvas = sourceCanvas;
    state.processedCanvas = null;
    state.latestRevision = -1;
    state.imageLoaded = false;

    dom.imageMeta.textContent = `${bitmap.width} x ${bitmap.height}px`;
    updateSummaryText();
    renderPreview();
    setFilterControlsEnabled(false);

    const ptr = state.wasm._malloc(imageData.data.byteLength);
    try {
        const heapU8 = getHeapU8();
        if (!heapU8) {
            throw new Error("HEAPU8 is unavailable on the WASM runtime");
        }

        heapU8.set(uploadBytes, ptr);
        call("editor_set_source_rgba", null, ["number", "number", "number", "number"], [ptr, bitmap.width, bitmap.height, bitmap.width * 4]);
        state.imageLoaded = true;
        setFilterControlsEnabled(true);
        updateSummaryText();
        setStatus("Processing");
        dom.statusMode.textContent = "Processing";
        ensurePumpLoop();
    } finally {
        state.wasm._free(ptr);
        if (typeof bitmap.close === "function") {
            bitmap.close();
        }
    }
}

function resetFilters() {
    dom.grayscaleSlider.value = "0";
    dom.blurSlider.value = "0";
    updateSliderLabels();
    updateSummaryText();

    if (!state.wasm || !state.imageLoaded) {
        return;
    }

    call("editor_set_grayscale_amount", null, ["number"], [0]);
    call("editor_set_blur_radius", null, ["number"], [0]);
    queueFilterUpdate();
}

function bindEvents() {
    dom.fileInput.addEventListener("change", async (event) => {
        const [file] = event.target.files || [];
        if (!file) {
            return;
        }

        try {
            await uploadImage(file);
        } catch (error) {
            state.imageLoaded = false;
            setFilterControlsEnabled(false);
            setStatus(`Error: ${error.message}`);
            dom.statusMode.textContent = "Error";
        }
    });

    dom.resetBtn.addEventListener("click", () => {
        resetFilters();
    });

    dom.grayscaleSlider.addEventListener("input", () => {
        updateSliderLabels();
        if (!state.wasm || !state.imageLoaded) {
            return;
        }

        call("editor_set_grayscale_amount", null, ["number"], [Number(dom.grayscaleSlider.value) / 100]);
        queueFilterUpdate();
    });

    dom.blurSlider.addEventListener("input", () => {
        updateSliderLabels();
        if (!state.wasm || !state.imageLoaded) {
            return;
        }

        call("editor_set_blur_radius", null, ["number"], [Number(dom.blurSlider.value)]);
        queueFilterUpdate();
    });

    window.addEventListener("resize", () => {
        renderPreview();
    });
}

async function bootstrap() {
    updateSliderLabels();
    updateSummaryText();
    bindEvents();

    try {
        state.wasm = await loadWasmRuntime();
        state.supportsCopyOutput = typeof state.wasm._editor_copy_output_rgba === "function";
        setFilterControlsEnabled(false);
        call("editor_init", null);
        refreshStatus();
        renderPreview();
    } catch (error) {
        setStatus(`Failed to initialize WASM: ${error.message}`);
        dom.statusMode.textContent = "Error";
    }
}

bootstrap();
