import { loadWasmRuntime } from "./wasm.js";

const MIN_NODES = 2;
const MAX_NODES = 32;
const DEFAULT_NODES = 10;
const VIEW_MARGIN = 72;
const ZOOM_STEP = 1.18;
const MIN_VIEW_FRACTION = 0.18;
const MAX_VIEW_MULTIPLIER = 4.5;

function required(id) {
    const element = document.getElementById(id);
    if (!element) {
        throw new Error(`Missing required DOM element: #${id}`);
    }
    return element;
}

const dom = {
    graphWrap: required("ufGraphWrap"),
    graph: required("ufGraph"),
    zoomInBtn: required("ufZoomInBtn"),
    zoomOutBtn: required("ufZoomOutBtn"),
    fitBtn: required("ufFitBtn"),
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
    viewport: {
        x: -VIEW_MARGIN,
        y: -VIEW_MARGIN,
        width: 960 + VIEW_MARGIN * 2,
        height: 560 + VIEW_MARGIN * 2,
    },
    viewportInitialized: false,
    isPanning: false,
    panPointerId: null,
    panStartClientX: 0,
    panStartClientY: 0,
    panStartViewportX: 0,
    panStartViewportY: 0,
    panStartViewportWidth: 0,
    panStartViewportHeight: 0,
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

function getGraphRect() {
    return dom.graphWrap.getBoundingClientRect();
}

function getGraphAspect() {
    const rect = getGraphRect();
    if (rect.width > 0 && rect.height > 0) return rect.width / rect.height;
    if (state.viewport.height > 0) return state.viewport.width / state.viewport.height;
    return 16 / 9;
}

function clampViewport(viewport) {
    const aspect = Math.max(0.2, getGraphAspect());
    const minWidth = Math.max(180, state.layoutWidth * MIN_VIEW_FRACTION);
    const maxWidth = Math.max(state.layoutWidth * MAX_VIEW_MULTIPLIER, state.layoutWidth + VIEW_MARGIN * 2);

    let width = Number.isFinite(viewport.width) ? viewport.width : state.layoutWidth;
    width = Math.max(minWidth, Math.min(maxWidth, width));
    let height = width / aspect;

    const minHeight = Math.max(120, state.layoutHeight * MIN_VIEW_FRACTION);
    const maxHeight = Math.max(state.layoutHeight * MAX_VIEW_MULTIPLIER, state.layoutHeight + VIEW_MARGIN * 2);
    if (height < minHeight) {
        height = minHeight;
        width = height * aspect;
    } else if (height > maxHeight) {
        height = maxHeight;
        width = height * aspect;
    }

    let x = Number.isFinite(viewport.x) ? viewport.x : (state.layoutWidth - width) / 2;
    let y = Number.isFinite(viewport.y) ? viewport.y : (state.layoutHeight - height) / 2;

    const panMargin = Math.max(48, Math.min(width, height) * 0.08);
    const minX = -panMargin;
    const maxX = state.layoutWidth - width + panMargin;
    const minY = -panMargin;
    const maxY = state.layoutHeight - height + panMargin;

    if (minX <= maxX) {
        x = Math.max(minX, Math.min(maxX, x));
    } else {
        x = (state.layoutWidth - width) / 2;
    }

    if (minY <= maxY) {
        y = Math.max(minY, Math.min(maxY, y));
    } else {
        y = (state.layoutHeight - height) / 2;
    }

    return { x, y, width, height };
}

function setViewport(viewport) {
    const clamped = clampViewport(viewport);
    state.viewport = clamped;
    state.viewportInitialized = true;
}

function applyViewport() {
    if (!state.viewportInitialized) {
        const aspect = Math.max(0.2, getGraphAspect());
        const contentWidth = state.layoutWidth + VIEW_MARGIN * 2;
        const contentHeight = state.layoutHeight + VIEW_MARGIN * 2;
        const contentAspect = contentWidth / contentHeight;
        let width = contentWidth;
        let height = contentHeight;
        if (contentAspect > aspect) {
            height = width / aspect;
        } else {
            width = height * aspect;
        }
        setViewport({
            x: (state.layoutWidth - width) / 2,
            y: (state.layoutHeight - height) / 2,
            width,
            height,
        });
    }

    dom.graph.setAttribute(
        "viewBox",
        `${state.viewport.x.toFixed(2)} ${state.viewport.y.toFixed(2)} ${state.viewport.width.toFixed(2)} ${state.viewport.height.toFixed(2)}`,
    );
}

function fitViewportToContent() {
    state.viewportInitialized = false;
    applyViewport();
}

function updateViewportForLayoutChange(previousLayoutWidth, previousLayoutHeight, forceFit = false) {
    if (forceFit || !state.viewportInitialized) {
        fitViewportToContent();
        return;
    }

    const previous = state.viewport;
    const zoomX = previous.width > 0 ? previousLayoutWidth / previous.width : 1;
    const zoomY = previous.height > 0 ? previousLayoutHeight / previous.height : 1;
    const zoom = Math.max(0.1, Math.min(zoomX, zoomY));
    const aspect = Math.max(0.2, getGraphAspect());
    let width = state.layoutWidth / zoom;
    let height = width / aspect;

    const centerRatioX = previousLayoutWidth > 0 ? (previous.x + previous.width / 2) / previousLayoutWidth : 0.5;
    const centerRatioY = previousLayoutHeight > 0 ? (previous.y + previous.height / 2) / previousLayoutHeight : 0.5;
    const centerX = centerRatioX * state.layoutWidth;
    const centerY = centerRatioY * state.layoutHeight;

    if (!Number.isFinite(height) || height <= 0) {
        height = state.layoutHeight;
        width = height * aspect;
    }

    setViewport({
        x: centerX - width / 2,
        y: centerY - height / 2,
        width,
        height,
    });
}

function zoomAtClient(clientX, clientY, factor) {
    if (!state.viewportInitialized) fitViewportToContent();
    const rect = dom.graph.getBoundingClientRect();
    if (rect.width <= 0 || rect.height <= 0) return;

    const sx = (clientX - rect.left) / rect.width;
    const sy = (clientY - rect.top) / rect.height;
    const clampedSx = Math.max(0, Math.min(1, sx));
    const clampedSy = Math.max(0, Math.min(1, sy));

    const targetWidth = state.viewport.width / factor;
    const worldX = state.viewport.x + clampedSx * state.viewport.width;
    const worldY = state.viewport.y + clampedSy * state.viewport.height;

    setViewport({
        x: worldX - clampedSx * targetWidth,
        y: worldY - clampedSy * (targetWidth / getGraphAspect()),
        width: targetWidth,
        height: targetWidth / getGraphAspect(),
    });
    renderGraph();
}

function zoomCentered(factor) {
    const rect = dom.graph.getBoundingClientRect();
    zoomAtClient(rect.left + rect.width / 2, rect.top + rect.height / 2, factor);
}

function beginPan(event) {
    if (event.button !== 0) return;
    if (!state.viewportInitialized) fitViewportToContent();
    state.isPanning = true;
    state.panPointerId = event.pointerId;
    state.panStartClientX = event.clientX;
    state.panStartClientY = event.clientY;
    state.panStartViewportX = state.viewport.x;
    state.panStartViewportY = state.viewport.y;
    state.panStartViewportWidth = state.viewport.width;
    state.panStartViewportHeight = state.viewport.height;
    dom.graphWrap.classList.add("dragging");
    if (typeof dom.graph.setPointerCapture === "function") {
        dom.graph.setPointerCapture(event.pointerId);
    }
    event.preventDefault();
}

function panToPointer(event) {
    if (!state.isPanning) return;
    if (event.pointerId !== state.panPointerId) return;

    const rect = getGraphRect();
    if (rect.width <= 0 || rect.height <= 0) return;

    const dx = event.clientX - state.panStartClientX;
    const dy = event.clientY - state.panStartClientY;
    setViewport({
        x: state.panStartViewportX - (dx / rect.width) * state.panStartViewportWidth,
        y: state.panStartViewportY - (dy / rect.height) * state.panStartViewportHeight,
        width: state.panStartViewportWidth,
        height: state.panStartViewportHeight,
    });
    renderGraph();
}

function endPan(event = null) {
    if (!state.isPanning) return;
    if (event && state.panPointerId !== null && event.pointerId !== state.panPointerId) return;

    if (event && typeof dom.graph.releasePointerCapture === "function") {
        try {
            dom.graph.releasePointerCapture(state.panPointerId);
        } catch {
            /* no-op */
        }
    }

    state.isPanning = false;
    state.panPointerId = null;
    dom.graphWrap.classList.remove("dragging");
}

function renderGraph() {
    if (state.count === 0 || state.positions.length !== state.count) {
        dom.graph.innerHTML = "";
        return;
    }

    applyViewport();
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
    state.count = state.module._uf_count();
    state.components = state.module._uf_component_count();
    state.parents = [];
    state.ranks = [];
    state.setSizes = [];

    for (let i = 0; i < state.count; i += 1) {
        state.parents.push(state.module._uf_get_parent(i));
        state.ranks.push(state.module._uf_get_rank(i));
        state.setSizes.push(state.module._uf_get_set_size(i));
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

async function syncFromWasm(previousParents, animateWhenChanged, forceFit = false) {
    const previousLayoutWidth = state.layoutWidth;
    const previousLayoutHeight = state.layoutHeight;
    pullStateFromWasm();
    const layout = computeTreeLayout(state.parents);
    state.layoutWidth = layout.width;
    state.layoutHeight = layout.height;
    const layoutChanged =
        previousLayoutWidth !== state.layoutWidth || previousLayoutHeight !== state.layoutHeight;
    if (forceFit || layoutChanged) {
        updateViewportForLayoutChange(previousLayoutWidth, previousLayoutHeight, forceFit);
    }

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
        state.module._uf_init(n);
        state.ops = 0;
        await syncFromWasm(null, false, true);
        setStatus(`Initialized ${n} singleton sets. Drag to pan, scroll to zoom.`);
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
        const root = state.module._uf_find(x);
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
        const merged = state.module._uf_union(a, b) === 1;
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
        const connected = state.module._uf_connected(a, b) === 1;
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
            merges += state.module._uf_union(a, b) === 1 ? 1 : 0;
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

    dom.graph.addEventListener(
        "wheel",
        (event) => {
            event.preventDefault();
            zoomAtClient(event.clientX, event.clientY, event.deltaY < 0 ? ZOOM_STEP : 1 / ZOOM_STEP);
        },
        { passive: false },
    );

    dom.graph.addEventListener("pointerdown", beginPan);
    window.addEventListener("pointermove", panToPointer);
    window.addEventListener("pointerup", (event) => endPan(event));
    window.addEventListener("pointercancel", (event) => endPan(event));
    window.addEventListener("blur", () => endPan());
    window.addEventListener("resize", () => {
        if (!state.viewportInitialized) return;
        setViewport(state.viewport);
        renderGraph();
    });

    dom.zoomInBtn.addEventListener("click", () => zoomCentered(1.24));
    dom.zoomOutBtn.addEventListener("click", () => zoomCentered(1 / 1.24));
    dom.fitBtn.addEventListener("click", () => {
        fitViewportToContent();
        renderGraph();
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
