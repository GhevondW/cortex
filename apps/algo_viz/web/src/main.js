import { getDom } from "./js/dom.js";
import { createState } from "./js/state.js";
import { loadWasmRuntime } from "./js/wasm.js";
import { createTreeRenderer } from "./js/treeRenderer.js";
import { createHashRenderer } from "./js/hashRenderer.js";

const dom = getDom();
const state = createState();
const renderer = createTreeRenderer(dom, state);
const hashRenderer = createHashRenderer(dom);
let wasmModule = null;

const SAMPLE_CONFIG = {
    bst: {
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
    },
    hash: {
        algorithms: [
            { id: 8, label: "Hash Insert", needsValue: true },
            { id: 9, label: "Hash Find", needsValue: true },
            { id: 10, label: "Hash Delete", needsValue: true },
        ],
        presets: [
            { id: 0, label: "Mixed keys" },
            { id: 1, label: "Collision-heavy keys" },
        ],
    },
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
    state.treeNodes = {};
    state.rootId = wasmModule.ccall("get_root_id", "number", [], []);
    const count = wasmModule.ccall("get_node_count", "number", [], []);
    for (let i = 0; i < count; i += 1) {
        state.treeNodes[i] = {
            value: wasmModule.ccall("get_node_value", "number", ["number"], [i]),
            left: wasmModule.ccall("get_node_left", "number", ["number"], [i]),
            right: wasmModule.ccall("get_node_right", "number", ["number"], [i]),
            parent: wasmModule.ccall("get_node_parent", "number", ["number"], [i]),
        };
    }
    dom.emptyMsg.style.display = state.rootId === -1 ? "flex" : "none";
}

function clearHighlightsAndOutput() {
    state.nodeHighlights = {};
    state.edgeHighlights = {};
    hashRenderer.clearHighlights();
    dom.traversalOutput.textContent = "\u2014";
}

function updateButtons() {
    const selectedId = Number.parseInt(dom.algorithmSelect.value, 10);
    const selectedAlgo = SAMPLE_CONFIG[dom.sampleSelect.value].algorithms.find((a) => a.id === selectedId);
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

function updateSelectOptions() {
    const sample = SAMPLE_CONFIG[dom.sampleSelect.value];
    dom.algorithmSelect.innerHTML = sample.algorithms.map((a) => `<option value="${a.id}">${a.label}</option>`).join("");
    dom.presetSelect.innerHTML = sample.presets.map((p) => `<option value="${p.id}">${p.label}</option>`).join("");
    const isHash = dom.sampleSelect.value === "hash";
    dom.canvas.hidden = isHash;
    dom.hashContainer.hidden = !isHash;
    dom.emptyMsg.hidden = isHash;
    const toolbar = dom.canvasContainer.querySelector(".canvas-toolbar");
    if (toolbar) {
        toolbar.hidden = isHash;
    }
    updateButtons();
}

function showHomePage() {
    dom.workspacePage.hidden = true;
    dom.homePage.hidden = false;
}

function showWorkspace(sample) {
    dom.homePage.hidden = true;
    dom.workspacePage.hidden = false;
    dom.sampleSelect.value = sample;
    updateSelectOptions();
    clearHighlightsAndOutput();
    if (!wasmModule || !state.isReady) {
        dom.statusText.textContent = "Initializing WASM module...";
        return;
    }
    if (sample === "hash") {
        wasmModule.ccall("build_preset_hash", null, ["number"], [0]);
    } else {
        wasmModule.ccall("build_preset_tree", null, ["number"], [0]);
    }
    rerenderFromWasm(true);
    dom.statusText.textContent = "Ready. Select an algorithm and click Run.";
}

async function scheduleStep() {
    if (!state.isRunning || state.isPaused) return;
    state.playTimer = setTimeout(async () => {
        if (!state.isRunning || state.isPaused) return;
        await wasmModule.ccall("step_algorithm", null, [], [], { async: true });
        state.stepCount += 1;
        dom.stepsEl.textContent = String(state.stepCount);
        if (wasmModule.ccall("is_algorithm_done", "number", [], []) !== 1) {
            scheduleStep();
        }
    }, state.currentSpeed);
}

async function startAlgorithm(autoPlay) {
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
    wasmModule.ccall("set_operation_value", null, ["number"], [value]);
    await wasmModule.ccall("start_algorithm", null, ["number"], [algoId], { async: true });
    state.stepCount += 1;
    dom.stepsEl.textContent = String(state.stepCount);
    if (wasmModule.ccall("is_algorithm_done", "number", [], []) === 1) return;
    if (autoPlay) scheduleStep();
}

async function doSingleStep() {
    if (!state.isRunning) {
        await startAlgorithm(false);
        return;
    }
    if (wasmModule.ccall("is_algorithm_done", "number", [], []) === 1) return;
    await wasmModule.ccall("step_algorithm", null, [], [], { async: true });
    state.stepCount += 1;
    dom.stepsEl.textContent = String(state.stepCount);
}

function rerenderFromWasm(fit = true) {
    if (dom.sampleSelect.value === "hash") {
        const size = wasmModule.ccall("get_hash_size", "number", [], []);
        const slots = [];
        for (let i = 0; i < size; i += 1) {
            slots.push({
                state: wasmModule.ccall("get_hash_slot_state", "number", ["number"], [i]),
                value: wasmModule.ccall("get_hash_slot_value", "number", ["number"], [i]),
            });
        }
        hashRenderer.setSlots(slots);
        return;
    }
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
        state.isRunning = false;
        state.isPaused = false;
        if (state.playTimer) {
            clearTimeout(state.playTimer);
            state.playTimer = null;
        }
        updateButtons();
        syncTreeFromWasm();
        renderer.drawTree();
    };

    window.onHashVisitSlot = (index) => {
        hashRenderer.highlight(index);
    };

    window.onHashClearHighlights = () => {
        hashRenderer.clearHighlights();
    };

    window.onHashSetSlot = (index, slotState, value) => {
        hashRenderer.setSlot(index, slotState, value);
    };

    window.onHashDone = () => {
        state.isRunning = false;
        state.isPaused = false;
        if (state.playTimer) {
            clearTimeout(state.playTimer);
            state.playTimer = null;
        }
        updateButtons();
        rerenderFromWasm(false);
    };
}

function wireEvents() {
    dom.openTreeBtn.addEventListener("click", () => showWorkspace("bst"));
    dom.openHashBtn.addEventListener("click", () => showWorkspace("hash"));
    dom.backHomeBtn.addEventListener("click", () => {
        if (state.playTimer) {
            clearTimeout(state.playTimer);
            state.playTimer = null;
        }
        state.isRunning = false;
        state.isPaused = false;
        updateButtons();
        showHomePage();
    });

    dom.speedSlider.addEventListener("input", updateSpeedDisplay);
    dom.algorithmSelect.addEventListener("change", updateButtons);
    dom.sampleSelect.addEventListener("change", () => {
        state.isRunning = false;
        state.isPaused = false;
        if (state.playTimer) {
            clearTimeout(state.playTimer);
            state.playTimer = null;
        }
        updateSelectOptions();
        clearHighlightsAndOutput();
        if (!wasmModule) return;
        if (dom.sampleSelect.value === "hash") {
            wasmModule.ccall("build_preset_hash", null, ["number"], [0]);
        } else {
            wasmModule.ccall("build_preset_tree", null, ["number"], [0]);
        }
        rerenderFromWasm(true);
    });
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
        if (state.playTimer) clearTimeout(state.playTimer);
        state.playTimer = null;
        state.isRunning = false;
        state.isPaused = false;
        wasmModule.ccall("reset_algorithm", null, [], []);
        clearHighlightsAndOutput();
        rerenderFromWasm(true);
        dom.statusText.textContent = "Reset. Ready.";
        updateButtons();
    });

    dom.loadPresetBtn.addEventListener("click", () => {
        const presetId = Number.parseInt(dom.presetSelect.value, 10);
        if (dom.sampleSelect.value === "hash") {
            wasmModule.ccall("build_preset_hash", null, ["number"], [presetId]);
        } else {
            wasmModule.ccall("build_preset_tree", null, ["number"], [presetId]);
        }
        clearHighlightsAndOutput();
        rerenderFromWasm(true);
        dom.statusText.textContent = "Preset loaded. Ready.";
    });

    dom.addNodeBtn.addEventListener("click", () => {
        const val = Number.parseInt(dom.addValueInput.value, 10);
        if (Number.isNaN(val)) return;
        if (dom.sampleSelect.value === "hash") {
            wasmModule.ccall("add_hash_value", null, ["number"], [val]);
        } else {
            wasmModule.ccall("add_node", null, ["number"], [val]);
        }
        clearHighlightsAndOutput();
        rerenderFromWasm(true);
        dom.addValueInput.value = "";
        dom.statusText.textContent = `Added ${val}. Ready.`;
    });

    dom.addValueInput.addEventListener("keydown", (e) => {
        if (e.key === "Enter") dom.addNodeBtn.click();
    });

    dom.randomBtn.addEventListener("click", () => {
        if (dom.sampleSelect.value === "hash") {
            wasmModule.ccall("clear_hash_table", null, [], []);
            const used = new Set();
            for (let i = 0; i < 10; i += 1) {
                let v = 0;
                do {
                    v = Math.floor(Math.random() * 120) + 1;
                } while (used.has(v));
                used.add(v);
                wasmModule.ccall("add_hash_value", null, ["number"], [v]);
            }
        } else {
            wasmModule.ccall("clear_tree", null, [], []);
            const used = new Set();
            for (let i = 0; i < 8; i += 1) {
                let v = 0;
                do {
                    v = Math.floor(Math.random() * 95) + 5;
                } while (used.has(v));
                used.add(v);
                wasmModule.ccall("add_node", null, ["number"], [v]);
            }
        }
        clearHighlightsAndOutput();
        rerenderFromWasm(true);
        dom.statusText.textContent = "Random tree generated. Ready.";
    });

    dom.clearBtn.addEventListener("click", () => {
        if (dom.sampleSelect.value === "hash") {
            wasmModule.ccall("clear_hash_table", null, [], []);
        } else {
            wasmModule.ccall("clear_tree", null, [], []);
        }
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
    updateSpeedDisplay();
    updateSelectOptions();
    showHomePage();
    updateButtons();

    try {
        wasmModule = await loadWasmRuntime();
        state.isReady = true;
        wasmModule.ccall("build_preset_tree", null, ["number"], [0]);
        renderer.resizeCanvas();
        dom.statusText.textContent = "Ready. Choose a sample from the home page.";
        updateButtons();
    } catch (error) {
        dom.statusText.textContent = `WASM failed to initialize: ${error.message}`;
        console.error(error);
    }
}

bootstrap();
