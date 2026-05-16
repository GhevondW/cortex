// Composition root. Instantiates every module, wires them together, and
// hands control to EditorApp.

import { loadWasmRuntime } from "./wasm.js";
import { EditorClient } from "./editor_client.js";
import { CanvasRenderer } from "./canvas.js";
import { TimelineControl } from "./timeline.js";
import { FilterControls, ApplyControls, SourceControls } from "./controls.js";
import { ProgressView } from "./progress.js";
import { SpinnerLiveness } from "./liveness.js";
import { VideoUploader } from "./uploader.js";
import { EditorApp } from "./editor_app.js";

const FRAME_WIDTH = 320;
const FRAME_HEIGHT = 240;
const FRAME_COUNT = 180;
const UPLOAD_MAX_FRAMES = 120;

async function main() {
    const $ = (id) => document.getElementById(id);

    const spinner = new SpinnerLiveness($("liveness-spinner"));
    spinner.start();

    const progress = new ProgressView($("progress-bar"), $("progress-label"), $("status-label"));
    progress.setStatus("Loading WASM…");

    let module;
    try {
        module = await loadWasmRuntime();
    } catch (err) {
        progress.setStatus(`Failed to load: ${err.message}`);
        return;
    }

    const client = new EditorClient(module);
    const canvas = new CanvasRenderer($("preview-canvas"));
    const timeline = new TimelineControl($("timeline-input"), $("timeline-label"));
    const filters = new FilterControls({
        brightness: $("slider-brightness"),
        contrast:   $("slider-contrast"),
        saturation: $("slider-saturation"),
        blur:       $("slider-blur"),
    });
    const apply = new ApplyControls({
        cooperative: $("btn-apply-cooperative"),
        blocking:    $("btn-apply-blocking"),
        cancel:      $("btn-cancel"),
        ab:          $("btn-ab"),
    });
    apply.setRunning(false);

    const source = new SourceControls({
        upload: $("upload-input"),
        reset:  $("btn-source-reset"),
        info:   $("source-info"),
    });

    const uploader = new VideoUploader({
        targetWidth:  FRAME_WIDTH,
        targetHeight: FRAME_HEIGHT,
        maxFrames:    UPLOAD_MAX_FRAMES,
    });

    progress.setStatus("Generating procedural frames…");
    // Defer to next frame so the status update paints.
    window.requestAnimationFrame(() => {
        const app = new EditorApp({
            client, canvas, timeline, filters, apply, progress, source, uploader,
        });
        app.init(FRAME_WIDTH, FRAME_HEIGHT, FRAME_COUNT);
    });
}

main();
