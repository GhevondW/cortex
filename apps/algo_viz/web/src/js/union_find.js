import { loadWasmRuntime } from "./wasm.js";

const MIN_NODES = 2;
const MAX_NODES = 32;
const DEFAULT_NODES = 10;

function required(id) {
    const element = document.getElementById(id);
    if (!element) {
        throw new Error(`Missing required DOM element: #${id}`);
    }
    return element;
}

const dom = {
    graph: required("ufGraph"),
    result: required("ufResult"),
    status: required("ufStatusText"),
    nodesEl: required("ufNodesEl"),
    componentsEl: required("ufComponentsEl"),
    opsEl: required("ufOpsEl"),
    sizeInput: required("ufSizeInput"),
    speedSlider: required("ufSpeedSlider"),
    speedLabel: required("ufSpeedLabel"),
    initBtn: required("ufInitBtn"),
    randomBtn: required("ufRandomBtn"),
    resetBtn: required("ufResetBtn"),
    findInput: required("ufFindInput"),
    findBtn: required("ufFindBtn"),
    unionAInput: required("ufUnionAInput"),
    unionBInput: required("ufUnionBInput"),
    unionBtn: required("ufUnionBtn"),
    connAInput: required("ufConnAInput"),
    connBInput: required("ufConnBInput"),
    connBtn: required("ufConnBtn"),
    stateDump: required("ufStateDump"),
};

const state = {
    module: null,
    count: 0,
    components: 0,
    parents: [],
    ranks: [],
    setSizes: [],
    ops: 0,
    positions: [],
    layoutWidth: 960,
    layoutHeight: 560,
    animationMs: 420,
    animationFrame: null,
    highlightNodes: new Set(),
    highlightEdges: new Set(),
    highlightTimer: null,
    isBusy: false,
};

function clampInt(value, min, max) {
    const parsed = Number.parseInt(String(value), 10);
    if (Number.isNaN(parsed)) return min;
    return Math.max(min, Math.min(max, parsed));
}

function mapSliderToDuration(sliderValue) {
    const minDelay = 140;
    const maxDelay = 1800;
    const t = (sliderValue - 1) / 99;
    return Math.round(maxDelay * Math.pow(minDelay / maxDelay, t));
}

function updateSpeedDisplay() {
    const sliderValue = clampInt(dom.speedSlider.value, 1, 100);
    dom.speedSlider.value = String(sliderValue);
    state.animationMs = mapSliderToDuration(sliderValue);
    dom.speedLabel.textContent = `${state.animationMs}ms`;
}

function setControlsEnabled(enabled) {
    const controls = [
        dom.sizeInput,
        dom.speedSlider,
        dom.initBtn,
        dom.randomBtn,
        dom.resetBtn,
        dom.findInput,
        dom.findBtn,
        dom.unionAInput,
        dom.unionBInput,
        dom.unionBtn,
        dom.connAInput,
        dom.connBInput,
        dom.connBtn,
    ];

    for (const element of controls) {
        element.disabled = !enabled;
    }
}

function setBusy(busy) {
    state.isBusy = busy;
    setControlsEnabled(Boolean(state.module) && !busy);
}

function setStatus(message) {
    dom.status.textContent = message;
}

function setResult(message) {
    dom.result.textContent = message;
}

function parseNodeIndex(input, label) {
    const value = Number.parseInt(input.value, 10);
    if (Number.isNaN(value)) {
        setStatus(`${label} must be a number between 0 and ${Math.max(state.count - 1, 0)}.`);
        return null;
    }
    if (value < 0 || value >= state.count) {
        setStatus(`${label} must be between 0 and ${Math.max(state.count - 1, 0)}.`);
        return null;
    }
    return value;
}

function arraysEqual(a, b) {
    if (a.length !== b.length) return false;
    for (let i = 0; i < a.length; i += 1) {
        if (a[i] !== b[i]) return false;
    }
    return true;
}

function getPath(index, parents) {
    if (index < 0 || index >= parents.length) return [];
    const path = [];
    const seen = new Set();
    let cursor = index;

    while (!seen.has(cursor) && cursor >= 0 && cursor < parents.length) {
        path.push(cursor);
        seen.add(cursor);
        const parent = parents[cursor];
        if (parent === cursor) break;
        cursor = parent;
    }

    return path;
}

function clearHighlightTimer() {
    if (!state.highlightTimer) return;
    clearTimeout(state.highlightTimer);
    state.highlightTimer = null;
}

function clearHighlights() {
    clearHighlightTimer();
    state.highlightNodes = new Set();
    state.highlightEdges = new Set();
}

function scheduleHighlightClear() {
    clearHighlightTimer();
    state.highlightTimer = setTimeout(() => {
        clearHighlights();
        renderGraph();
    }, Math.max(300, Math.round(state.animationMs * 1.1)));
}

function setHighlights(nodes, edges, autoClear = true) {
    clearHighlights();
    for (const node of nodes) state.highlightNodes.add(node);
    for (const edge of edges) state.highlightEdges.add(edge);
    renderGraph();
    if (autoClear) scheduleHighlightClear();
}

function stopAnimation() {
    if (!state.animationFrame) return;
    cancelAnimationFrame(state.animationFrame);
    state.animationFrame = null;
}

function computeTreeLayout(parents) {
    const count = parents.length;
    const width = Math.max(960, count * 68 + 120);
    const height = 560;
    const marginX = 56;
    const topY = 72;
    const bottomMargin = 86;

    if (count === 0) {
        return { width, height, positions: [] };
    }

    const children = Array.from({ length: count }, () => []);
    const roots = [];
    for (let i = 0; i < count; i += 1) {
        const parent = parents[i];
        if (parent >= 0 && parent < count && parent !== i) {
            children[parent].push(i);
        } else {
            roots.push(i);
        }
    }

    for (const branch of children) {
        branch.sort((a, b) => a - b);
    }
    roots.sort((a, b) => a - b);

    const xUnits = new Array(count).fill(0);
    const depths = new Array(count).fill(0);
    const visited = new Array(count).fill(false);
    let cursor = 0;

    function dfs(node, depth) {
        if (visited[node]) return;
        visited[node] = true;
        depths[node] = depth;
        const branch = children[node];
        if (branch.length === 0) {
            xUnits[node] = cursor;
            cursor += 1;
            return;
        }

        for (const child of branch) {
            dfs(child, depth + 1);
        }
        const left = xUnits[branch[0]];
        const right = xUnits[branch[branch.length - 1]];
        xUnits[node] = (left + right) / 2;
    }

    for (const root of roots) {
        if (visited[root]) continue;
        if (cursor > 0) cursor += 1;
        dfs(root, 0);
    }

    for (let node = 0; node < count; node += 1) {
        if (visited[node]) continue;
        if (cursor > 0) cursor += 1;
        dfs(node, 0);
    }

    const minX = Math.min(...xUnits);
    const maxX = Math.max(...xUnits);
    const maxDepth = Math.max(...depths);
    const usableW = width - marginX * 2;
    const usableH = height - topY - bottomMargin;
    const levelGap = maxDepth === 0 ? 0 : Math.max(56, Math.min(132, usableH / maxDepth));

    const positions = [];
    for (let i = 0; i < count; i += 1) {
        const ratio = maxX === minX ? 0.5 : (xUnits[i] - minX) / (maxX - minX);
        positions.push({
            x: marginX + ratio * usableW,
            y: topY + depths[i] * levelGap,
        });
    }

    return { width, height, positions };
}

function nodeRadius() {
    return Math.max(10, Math.min(22, 24 - Math.floor(state.count / 3)));
}

function renderGraph() {
    if (state.count === 0 || state.positions.length !== state.count) {
        dom.graph.innerHTML = "";
        return;
    }

    dom.graph.setAttribute("viewBox", `0 0 ${state.layoutWidth} ${state.layoutHeight}`);
    const radius = nodeRadius();

    const edges = [];
    for (let i = 0; i < state.count; i += 1) {
        const parent = state.parents[i];
        if (parent === i || parent < 0 || parent >= state.count) continue;

        const from = state.positions[parent];
        const to = state.positions[i];
        const key = `${i}-${parent}`;
        const highlighted = state.highlightEdges.has(key);
        const clazz = highlighted ? "uf-edge uf-edge-highlight" : "uf-edge";
        const x1 = from.x;
        const y1 = from.y + radius;
        const x2 = to.x;
        const y2 = to.y - radius;
        edges.push(`<line class="${clazz}" x1="${x1.toFixed(2)}" y1="${y1.toFixed(2)}" x2="${x2.toFixed(2)}" y2="${y2.toFixed(2)}"></line>`);
    }

    const compact = state.count > 18;
    const nodes = [];
    for (let i = 0; i < state.count; i += 1) {
        const point = state.positions[i];
        const isRoot = state.parents[i] === i;
        const isHighlighted = state.highlightNodes.has(i);
        let clazz = "uf-node";
        if (isRoot) clazz += " uf-node-root";
        if (isHighlighted) clazz += " uf-node-highlight";

        const subLabel = compact ? `p:${state.parents[i]}` : `p:${state.parents[i]} r:${state.ranks[i]}`;

        nodes.push(
            `<g>
                <circle class="${clazz}" cx="${point.x.toFixed(2)}" cy="${point.y.toFixed(2)}" r="${radius.toFixed(2)}"></circle>
                <text class="uf-node-label" x="${point.x.toFixed(2)}" y="${(point.y + 1).toFixed(2)}">${i}</text>
                <text class="uf-sub-label" x="${point.x.toFixed(2)}" y="${(point.y + radius + 15).toFixed(2)}">${subLabel}</text>
            </g>`,
        );
    }

    dom.graph.innerHTML = `${edges.join("")}${nodes.join("")}`;
}

function refreshStateDump() {
    const parentStr = `[${state.parents.join(", ")}]`;
    const rankStr = `[${state.ranks.join(", ")}]`;
    const sizeStr = `[${state.setSizes.join(", ")}]`;
    dom.stateDump.textContent = `parent: ${parentStr}\nrank:   ${rankStr}\nsize:   ${sizeStr}`;
}

function refreshInputBounds() {
    const maxIndex = Math.max(0, state.count - 1);
    const indexInputs = [dom.findInput, dom.unionAInput, dom.unionBInput, dom.connAInput, dom.connBInput];
    for (const input of indexInputs) {
        input.max = String(maxIndex);
        const current = Number.parseInt(input.value, 10);
        if (Number.isNaN(current)) {
            input.value = "0";
            continue;
        }
        input.value = String(Math.max(0, Math.min(current, maxIndex)));
    }
}

function pullStateFromWasm() {
    state.count = state.module.ccall("uf_count", "number", [], []);
    state.components = state.module.ccall("uf_component_count", "number", [], []);
    state.parents = [];
    state.ranks = [];
    state.setSizes = [];

    for (let i = 0; i < state.count; i += 1) {
        state.parents.push(state.module.ccall("uf_get_parent", "number", ["number"], [i]));
        state.ranks.push(state.module.ccall("uf_get_rank", "number", ["number"], [i]));
        state.setSizes.push(state.module.ccall("uf_get_set_size", "number", ["number"], [i]));
    }

    dom.nodesEl.textContent = String(state.count);
    dom.componentsEl.textContent = String(state.components);
    dom.opsEl.textContent = String(state.ops);
    refreshInputBounds();
    refreshStateDump();
}

function animateToPositions(targetPositions) {
    stopAnimation();

    if (state.positions.length !== targetPositions.length) {
        state.positions = targetPositions.map((position) => ({ ...position }));
        renderGraph();
        return Promise.resolve();
    }

    const fromPositions = state.positions.map((position) => ({ ...position }));
    const duration = Math.max(100, state.animationMs);
    const start = performance.now();

    return new Promise((resolve) => {
        const tick = (now) => {
            const t = Math.min(1, (now - start) / duration);
            const eased = t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2;
            state.positions = targetPositions.map((target, index) => {
                const from = fromPositions[index];
                return {
                    x: from.x + (target.x - from.x) * eased,
                    y: from.y + (target.y - from.y) * eased,
                };
            });
            renderGraph();

            if (t >= 1) {
                state.animationFrame = null;
                resolve();
                return;
            }
            state.animationFrame = requestAnimationFrame(tick);
        };

        state.animationFrame = requestAnimationFrame(tick);
    });
}

async function syncFromWasm(previousParents, animateWhenChanged) {
    pullStateFromWasm();
    const layout = computeTreeLayout(state.parents);
    state.layoutWidth = layout.width;
    state.layoutHeight = layout.height;

    const parentsChanged = previousParents ? !arraysEqual(previousParents, state.parents) : true;
    if (state.positions.length === 0 || state.positions.length !== layout.positions.length) {
        state.positions = layout.positions.map((position) => ({ ...position }));
        renderGraph();
        return;
    }

    if (animateWhenChanged && parentsChanged) {
        await animateToPositions(layout.positions);
        return;
    }

    state.positions = layout.positions.map((position) => ({ ...position }));
    renderGraph();
}

async function initializeDataset() {
    if (!state.module || state.isBusy) return;

    setBusy(true);
    try {
        stopAnimation();
        clearHighlights();
        const n = clampInt(dom.sizeInput.value, MIN_NODES, MAX_NODES);
        dom.sizeInput.value = String(n);
        state.module.ccall("uf_init", null, ["number"], [n]);
        state.ops = 0;
        await syncFromWasm(null, false);
        setStatus(`Initialized ${n} singleton sets.`);
        setResult("Use Find / Union / Connected operations.");
    } finally {
        setBusy(false);
    }
}

async function runFind() {
    if (!state.module || state.isBusy) return;
    const x = parseNodeIndex(dom.findInput, "Find index");
    if (x === null) return;

    setBusy(true);
    try {
        const beforeParents = state.parents.slice();
        const beforePath = getPath(x, beforeParents);
        const root = state.module.ccall("uf_find", "number", ["number"], [x]);
        state.ops += 1;
        await syncFromWasm(beforeParents, true);

        const path = getPath(x, state.parents).length > 0 ? getPath(x, state.parents) : beforePath;
        const edges = [];
        for (let i = 0; i + 1 < path.length; i += 1) {
            const parent = state.parents[path[i]];
            if (parent !== path[i]) edges.push(`${path[i]}-${parent}`);
        }
        setHighlights(path, edges, true);
        setStatus(`find(${x}) -> root ${root}.`);
        setResult(`Root(${x}) = ${root}`);
    } finally {
        setBusy(false);
    }
}

async function runUnion() {
    if (!state.module || state.isBusy) return;
    const a = parseNodeIndex(dom.unionAInput, "Union A");
    if (a === null) return;
    const b = parseNodeIndex(dom.unionBInput, "Union B");
    if (b === null) return;

    setBusy(true);
    try {
        const beforeParents = state.parents.slice();
        const pathA = getPath(a, beforeParents);
        const pathB = getPath(b, beforeParents);
        const merged = state.module.ccall("uf_union", "number", ["number", "number"], [a, b]) === 1;
        state.ops += 1;
        await syncFromWasm(beforeParents, true);

        const nodes = [...new Set([...pathA, ...pathB])];
        const edges = [];
        for (const node of nodes) {
            const parent = state.parents[node];
            if (parent !== node) edges.push(`${node}-${parent}`);
        }
        setHighlights(nodes, edges, true);

        if (merged) {
            setStatus(`union(${a}, ${b}) merged two sets.`);
            setResult(`Merged sets containing ${a} and ${b}.`);
        } else {
            setStatus(`union(${a}, ${b}) skipped. Already in the same set.`);
            setResult(`${a} and ${b} are already connected.`);
        }
    } finally {
        setBusy(false);
    }
}

async function runConnected() {
    if (!state.module || state.isBusy) return;
    const a = parseNodeIndex(dom.connAInput, "Connected A");
    if (a === null) return;
    const b = parseNodeIndex(dom.connBInput, "Connected B");
    if (b === null) return;

    setBusy(true);
    try {
        const beforeParents = state.parents.slice();
        const connected = state.module.ccall("uf_connected", "number", ["number", "number"], [a, b]) === 1;
        state.ops += 1;
        await syncFromWasm(beforeParents, true);
        setHighlights([a, b], [], true);
        setStatus(`connected(${a}, ${b}) -> ${connected ? "true" : "false"}.`);
        setResult(connected ? `${a} and ${b} are connected.` : `${a} and ${b} are not connected.`);
    } finally {
        setBusy(false);
    }
}

async function runRandomUnions() {
    if (!state.module || state.isBusy || state.count < 2) return;

    setBusy(true);
    try {
        stopAnimation();
        clearHighlights();
        const beforeParents = state.parents.slice();
        const trials = Math.max(1, Math.floor(state.count / 2));
        let merges = 0;

        for (let i = 0; i < trials; i += 1) {
            const a = Math.floor(Math.random() * state.count);
            let b = Math.floor(Math.random() * state.count);
            if (a === b) b = (b + 1) % state.count;
            merges += state.module.ccall("uf_union", "number", ["number", "number"], [a, b]) === 1 ? 1 : 0;
        }

        state.ops += trials;
        await syncFromWasm(beforeParents, true);
        setStatus(`Executed ${trials} random union operations (${merges} merges).`);
        setResult("Random unions complete.");
    } finally {
        setBusy(false);
    }
}

function wireEvents() {
    dom.speedSlider.addEventListener("input", updateSpeedDisplay);
    dom.initBtn.addEventListener("click", () => void initializeDataset());
    dom.randomBtn.addEventListener("click", () => void runRandomUnions());
    dom.resetBtn.addEventListener("click", () => void initializeDataset());
    dom.findBtn.addEventListener("click", () => void runFind());
    dom.unionBtn.addEventListener("click", () => void runUnion());
    dom.connBtn.addEventListener("click", () => void runConnected());

    dom.sizeInput.addEventListener("keydown", (event) => {
        if (event.key === "Enter") void initializeDataset();
    });
    dom.findInput.addEventListener("keydown", (event) => {
        if (event.key === "Enter") void runFind();
    });
    dom.unionAInput.addEventListener("keydown", (event) => {
        if (event.key === "Enter") void runUnion();
    });
    dom.unionBInput.addEventListener("keydown", (event) => {
        if (event.key === "Enter") void runUnion();
    });
    dom.connAInput.addEventListener("keydown", (event) => {
        if (event.key === "Enter") void runConnected();
    });
    dom.connBInput.addEventListener("keydown", (event) => {
        if (event.key === "Enter") void runConnected();
    });
}

async function bootstrap() {
    setControlsEnabled(false);
    updateSpeedDisplay();
    wireEvents();
    renderGraph();

    try {
        state.module = await loadWasmRuntime();
        setControlsEnabled(true);
        dom.sizeInput.value = String(DEFAULT_NODES);
        await initializeDataset();
    } catch (error) {
        setStatus(`WASM failed to initialize: ${error.message}`);
        setResult("Runtime initialization failed.");
        console.error(error);
    }
}

bootstrap();
