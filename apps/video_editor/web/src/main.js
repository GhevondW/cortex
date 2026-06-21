// Composition root. Loads the WASM runtime, instantiates every module, wires
// them together, and hands control to the live EditorApp.

import { loadWasmRuntime } from "./wasm.js";
import { EditorClient } from "./editor_client.js";
import { CanvasRenderer } from "./canvas.js";
import { TimelineControl } from "./timeline.js";
import { FilterControls, EngineControls, SourceControls } from "./controls.js";
import { ProgressView } from "./progress.js";
import { SpinnerLiveness } from "./liveness.js";
import { Playback } from "./playback.js";
import { EditorApp } from "./editor_app.js";

// Procedural sample dimensions / length. Uploaded videos get their own working
// resolution (see VideoProvider / fitWorkingSize).
const FRAME_WIDTH = 320;
const FRAME_HEIGHT = 240;
const FRAME_COUNT = 180;

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
    const timeline = new TimelineControl($("timeline-input"), $("timeline-label"), $("btn-play"));
    const filters = new FilterControls({
        brightness: $("slider-brightness"),
        contrast:   $("slider-contrast"),
        saturation: $("slider-saturation"),
        blur:       $("slider-blur"),
    });
    const engine = new EngineControls($("chk-cooperative"));
    const source = new SourceControls({
        open:   $("open-input"),
        sample: $("btn-use-sample"),
        info:   $("source-info"),
    });
    const playback = new Playback({ client, canvas });

    const app = new EditorApp({
        client, canvas, timeline, filters, engine, source, progress, playback,
    });
    app.init(FRAME_WIDTH, FRAME_HEIGHT, FRAME_COUNT);
}

main();
