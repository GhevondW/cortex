import { getDom } from "./js/dom.js";
import { createState } from "./js/state.js";
import { loadWasmRuntime } from "./js/wasm.js";
import { createTreeRenderer } from "./js/treeRenderer.js";

const dom = getDom();
const state = createState();
const renderer = createTreeRenderer(dom, state);
let wasmModule = null;

const BST_CONFIG = {
    algorithms: [
        { id: 0, label: "BST Insert", needsValue: true },
        { id: 1, label: "BST Delete", needsValue: true },
        { id: 2, label: "BST Find", needsValue: true },
        { id: 3, label: "In-order Traversal", needsValue: false },
        { id: 4, label: "Pre-order Traversal", needsValue: false },
        { id: 5, label: "Post-order Traversal", needsValue: false },
        { id: 6, label: "BFS (Level-order)", needsValue: false },
        { id: 7, label: "DFS (Depth-first)", needsValue: false },
    ],
    presets: [
        { id: 0, label: "Balanced (7 nodes)" },
        { id: 1, label: "Skewed Left (5 nodes)" },
        { id: 2, label: "Skewed Right (5 nodes)" },
        { id: 3, label: "Larger (10 nodes)" },
    ],
};

function getDelay(sliderValue) {
    const minDelay = 40;
    const maxDelay = 2000;
    const t = (sliderValue - 1) / 99;
    return Math.round(maxDelay * Math.pow(minDelay / maxDelay, t));
}

function updateSpeedDisplay() {
    const delay = getDelay(Number.parseInt(dom.speedSlider.value, 10));
    state.currentSpeed = delay;
    dom.speedLabel.textContent = delay >= 1000 ? `${(delay / 1000).toFixed(1)}s` : `${delay}ms`;
}

function syncTreeFromWasm() {
    if (!wasmModule) return;
    state.treeNodes = {};
    state.rootId = wasmModule._get_root_id();
    const count = wasmModule._get_node_count();
    for (let i = 0; i < count; i += 1) {
        state.treeNodes[i] = {
            value: wasmModule._get_node_value(i),
            left: wasmModule._get_node_left(i),
            right: wasmModule._get_node_right(i),
            parent: wasmModule._get_node_parent(i),
        };
    }
    dom.emptyMsg.style.display = state.rootId === -1 ? "flex" : "none";
}

function clearHighlightsAndOutput() {
    state.nodeHighlights = {};
    state.edgeHighlights = {};
    dom.traversalOutput.textContent = "\u2014";
}

function stopPlayback() {
    state.isRunning = false;
    state.isPaused = false;
    if (state.playTimer) {
        clearTimeout(state.playTimer);
        state.playTimer = null;
    }
}

function resetExecution() {
    stopPlayback();
    if (wasmModule) {
        wasmModule._reset_algorithm();
    }
}

function getSelectedAlgorithm() {
    const selectedId = Number.parseInt(dom.algorithmSelect.value, 10);
    return BST_CONFIG.algorithms.find((algorithm) => algorithm.id === selectedId) || null;
}

function updateButtons() {
    const selectedAlgo = getSelectedAlgorithm();
    const needsValue = selectedAlgo ? selectedAlgo.needsValue : true;
    dom.valueInputGroup.style.display = needsValue ? "" : "none";

    if (state.isRunning) {
        dom.runBtn.disabled = true;
        dom.stepBtn.disabled = !state.isPaused;
        dom.pauseBtn.disabled = false;
        dom.pauseBtn.textContent = state.isPaused ? "Resume" : "Pause";
        dom.resetBtn.disabled = false;
        dom.loadPresetBtn.disabled = true;
        dom.addNodeBtn.disabled = true;
        dom.randomBtn.disabled = true;
        dom.clearBtn.disabled = true;
        return;
    }

    dom.runBtn.disabled = !state.isReady;
    dom.stepBtn.disabled = !state.isReady;
    dom.pauseBtn.disabled = true;
    dom.pauseBtn.textContent = "Pause";
    dom.resetBtn.disabled = true;
    dom.loadPresetBtn.disabled = !state.isReady;
    dom.addNodeBtn.disabled = !state.isReady;
    dom.randomBtn.disabled = !state.isReady;
    dom.clearBtn.disabled = !state.isReady;
}

function populateControls() {
    dom.algorithmSelect.innerHTML = BST_CONFIG.algorithms
        .map((algorithm) => `<option value="${algorithm.id}">${algorithm.label}</option>`)
        .join("");
    dom.presetSelect.innerHTML = BST_CONFIG.presets
        .map((preset) => `<option value="${preset.id}">${preset.label}</option>`)
        .join("");
    updateButtons();
}

function loadPreset(presetId, fit = true) {
    if (!wasmModule) return;
    resetExecution();
    wasmModule._build_preset_tree(presetId);
    clearHighlightsAndOutput();
    dom.comparisonsEl.textContent = "0";
    dom.stepsEl.textContent = "0";
    rerenderFromWasm(fit);
}

async function scheduleStep() {
    if (!state.isRunning || state.isPaused) return;
    state.playTimer = setTimeout(async () => {
        if (!state.isRunning || state.isPaused) return;
        await wasmModule._step_algorithm();
        state.stepCount += 1;
        dom.stepsEl.textContent = String(state.stepCount);
        if (wasmModule._is_algorithm_done() !== 1) {
            scheduleStep();
        }
    }, state.currentSpeed);
}

async function startAlgorithm(autoPlay) {
    if (!wasmModule || !state.isReady) return;
    if (state.isRunning) return;
    state.isRunning = true;
    state.isPaused = !autoPlay;
    state.stepCount = 0;
    state.comparisons = 0;
    dom.stepsEl.textContent = "0";
    dom.comparisonsEl.textContent = "0";
    clearHighlightsAndOutput();
    updateButtons();

    const algoId = Number.parseInt(dom.algorithmSelect.value, 10);
    const value = Number.parseInt(dom.valueInput.value, 10) || 0;
    wasmModule._set_operation_value(value);
    await wasmModule._start_algorithm(algoId);
    state.stepCount += 1;
    dom.stepsEl.textContent = String(state.stepCount);
    if (wasmModule._is_algorithm_done() === 1) return;
    if (autoPlay) scheduleStep();
}

async function doSingleStep() {
    if (!wasmModule || !state.isReady) return;
    if (!state.isRunning) {
        await startAlgorithm(false);
        return;
    }
    if (wasmModule._is_algorithm_done() === 1) return;
    await wasmModule._step_algorithm();
    state.stepCount += 1;
    dom.stepsEl.textContent = String(state.stepCount);
}

function rerenderFromWasm(fit = true) {
    if (!wasmModule) return;
    syncTreeFromWasm();
    renderer.layoutTree();
    if (fit) renderer.fitTreeToView();
    renderer.drawTree();
}

function registerWasmCallbacks() {
    window.onTreeVisit = (nodeId, color) => {
        state.nodeHighlights[nodeId] = color;
        if (color === 1) {
            state.comparisons += 1;
            dom.comparisonsEl.textContent = String(state.comparisons);
        }
        renderer.drawTree();
    };

    window.onTreeUnvisitAll = () => {
        state.nodeHighlights = {};
        renderer.drawTree();
    };

    window.onTreeAddNode = (nodeId, value, parentId, isLeft) => {
        state.treeNodes[nodeId] = { value, left: -1, right: -1, parent: parentId };
        if (parentId !== -1 && state.treeNodes[parentId]) {
            state.treeNodes[parentId][isLeft ? "left" : "right"] = nodeId;
        }
        if (state.rootId === -1) state.rootId = nodeId;
        dom.emptyMsg.style.display = "none";
        renderer.layoutTree();
        renderer.drawTree();
    };

    window.onTreeRemoveNode = () => {
        syncTreeFromWasm();
        renderer.layoutTree();
        renderer.drawTree();
    };

    window.onTreeUpdateValue = (nodeId, newValue) => {
        if (state.treeNodes[nodeId]) state.treeNodes[nodeId].value = newValue;
        renderer.drawTree();
    };

    window.onTreeHighlightEdge = (fromId, toId, color) => {
        state.edgeHighlights[`${fromId}-${toId}`] = color;
        renderer.drawTree();
    };

    window.onTreeUnhighlightEdges = () => {
        state.edgeHighlights = {};
        renderer.drawTree();
    };

    window.onTreeLog = (msg) => {
        dom.statusText.textContent = msg;
    };

    window.onTreeAppendResult = (value) => {
        if (dom.traversalOutput.textContent === "\u2014" || dom.traversalOutput.textContent === "") {
            dom.traversalOutput.textContent = String(value);
            return;
        }
        dom.traversalOutput.textContent += ` -> ${value}`;
    };

    window.onTreeClearResult = () => {
        dom.traversalOutput.textContent = "\u2014";
    };

    window.onTreeDone = () => {
        stopPlayback();
        updateButtons();
        syncTreeFromWasm();
        renderer.drawTree();
    };
}

function wireEvents() {
    dom.speedSlider.addEventListener("input", updateSpeedDisplay);
    dom.algorithmSelect.addEventListener("change", updateButtons);
    dom.runBtn.addEventListener("click", () => startAlgorithm(true));
    dom.stepBtn.addEventListener("click", () => doSingleStep());

    dom.pauseBtn.addEventListener("click", () => {
        if (!state.isRunning) return;
        state.isPaused = !state.isPaused;
        updateButtons();
        if (!state.isPaused) {
            scheduleStep();
        } else if (state.playTimer) {
            clearTimeout(state.playTimer);
            state.playTimer = null;
        }
    });

    dom.resetBtn.addEventListener("click", () => {
        resetExecution();
        clearHighlightsAndOutput();
        rerenderFromWasm(true);
        dom.statusText.textContent = "Reset. Ready.";
        updateButtons();
    });

    dom.loadPresetBtn.addEventListener("click", () => {
        if (!wasmModule) return;
        const presetId = Number.parseInt(dom.presetSelect.value, 10);
        loadPreset(presetId, true);
        dom.statusText.textContent = "Preset loaded. Ready.";
    });

    dom.addNodeBtn.addEventListener("click", () => {
        if (!wasmModule) return;
        const val = Number.parseInt(dom.addValueInput.value, 10);
        if (Number.isNaN(val)) return;
        resetExecution();
        wasmModule._add_node(val);
        clearHighlightsAndOutput();
        rerenderFromWasm(true);
        dom.addValueInput.value = "";
        dom.statusText.textContent = `Added ${val}. Ready.`;
    });

    dom.addValueInput.addEventListener("keydown", (e) => {
        if (e.key === "Enter") dom.addNodeBtn.click();
    });

    dom.randomBtn.addEventListener("click", () => {
        if (!wasmModule) return;
        resetExecution();
        wasmModule._clear_tree();
        const used = new Set();
        for (let i = 0; i < 8; i += 1) {
            let v = 0;
            do {
                v = Math.floor(Math.random() * 95) + 5;
            } while (used.has(v));
            used.add(v);
            wasmModule._add_node(v);
        }
        clearHighlightsAndOutput();
        rerenderFromWasm(true);
        dom.statusText.textContent = "Random tree generated. Ready.";
    });

    dom.clearBtn.addEventListener("click", () => {
        if (!wasmModule) return;
        resetExecution();
        wasmModule._clear_tree();
        clearHighlightsAndOutput();
        rerenderFromWasm(true);
        dom.statusText.textContent = "Tree cleared. Ready.";
    });

    dom.zoomInBtn.addEventListener("click", () => {
        const rect = dom.canvasContainer.getBoundingClientRect();
        renderer.zoomAt(rect.width / 2, rect.height / 2, 1.3);
    });

    dom.zoomOutBtn.addEventListener("click", () => {
        const rect = dom.canvasContainer.getBoundingClientRect();
        renderer.zoomAt(rect.width / 2, rect.height / 2, 1 / 1.3);
    });

    dom.fitBtn.addEventListener("click", () => {
        renderer.fitTreeToView();
        renderer.drawTree();
    });
    dom.spreadHBtn.addEventListener("click", () => renderer.spreadTree(1.4, 1));
    dom.shrinkHBtn.addEventListener("click", () => renderer.spreadTree(1 / 1.4, 1));
    dom.spreadVBtn.addEventListener("click", () => renderer.spreadTree(1, 1.4));
    dom.shrinkVBtn.addEventListener("click", () => renderer.spreadTree(1, 1 / 1.4));
}

async function bootstrap() {
    registerWasmCallbacks();
    wireEvents();
    renderer.bindCanvasGestures();
    populateControls();
    updateSpeedDisplay();
    updateButtons();
    renderer.resizeCanvas();

    try {
        wasmModule = await loadWasmRuntime();
        state.isReady = true;
        loadPreset(0, true);
        dom.statusText.textContent = "Ready. Select an algorithm and click Run.";
        updateButtons();
    } catch (error) {
        dom.statusText.textContent = `WASM failed to initialize: ${error.message}`;
        console.error(error);
    }
}

bootstrap();
