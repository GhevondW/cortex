const NODE_RADIUS = 22;
const VERTICAL_GAP = 68;
const MIN_X_GAP = 32;

const VIZ_COLORS = {
    0: "#3b82f6",
    1: "#f59e0b",
    2: "#fb923c",
    3: "#16a34a",
    4: "#22c55e",
    5: "#ef4444",
    6: "#06b6d4",
    7: "#8b5cf6",
    8: "#dc2626",
};

const DEFAULT_NODE_COLOR = "#3b82f6";
const DEFAULT_EDGE_COLOR = "#cbd5e1";

export function createTreeRenderer(dom, state) {
    const ctx = dom.canvas.getContext("2d");
    const cam = { x: 0, y: 0, scale: 1 };
    let isDragging = false;
    let dragStart = { x: 0, y: 0 };
    let camStart = { x: 0, y: 0 };
    let lastTouch = null;

    function resizeCanvas() {
        const dpr = window.devicePixelRatio || 1;
        const rect = dom.canvasContainer.getBoundingClientRect();
        dom.canvas.width = rect.width * dpr;
        dom.canvas.height = rect.height * dpr;
        dom.canvas.style.width = `${rect.width}px`;
        dom.canvas.style.height = `${rect.height}px`;
        ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
        layoutTree();
        fitTreeToView();
        drawTree();
    }

    function zoomAt(screenX, screenY, factor) {
        const wx = screenX / cam.scale - cam.x;
        const wy = screenY / cam.scale - cam.y;
        cam.scale = Math.min(Math.max(cam.scale * factor, 0.15), 4);
        cam.x = screenX / cam.scale - wx;
        cam.y = screenY / cam.scale - wy;
        drawTree();
    }

    function spreadTree(factorX, factorY) {
        const positions = Object.values(state.nodePositions);
        if (positions.length === 0) return;

        let cx = 0;
        let cy = 0;
        for (const p of positions) {
            cx += p.x;
            cy += p.y;
        }
        cx /= positions.length;
        cy /= positions.length;

        for (const p of positions) {
            p.x = cx + (p.x - cx) * factorX;
            p.y = cy + (p.y - cy) * factorY;
        }
        drawTree();
    }

    function fitTreeToView() {
        const positions = Object.values(state.nodePositions);
        if (positions.length === 0) {
            cam.x = 0;
            cam.y = 0;
            cam.scale = 1;
            return;
        }

        const rect = dom.canvasContainer.getBoundingClientRect();
        const pad = NODE_RADIUS + 20;
        let minX = Infinity;
        let maxX = -Infinity;
        let minY = Infinity;
        let maxY = -Infinity;

        for (const p of positions) {
            minX = Math.min(minX, p.x);
            maxX = Math.max(maxX, p.x);
            minY = Math.min(minY, p.y);
            maxY = Math.max(maxY, p.y);
        }

        const treeW = maxX - minX + pad * 2;
        const treeH = maxY - minY + pad * 2;
        cam.scale = Math.min(rect.width / treeW, rect.height / treeH, 1.5);
        const centerX = (minX + maxX) / 2;
        const centerY = (minY + maxY) / 2;
        cam.x = rect.width / (2 * cam.scale) - centerX;
        cam.y = rect.height / (2 * cam.scale) - centerY;
    }

    function getTreeDepth(id) {
        if (id === -1 || !state.treeNodes[id]) return 0;
        return 1 + Math.max(getTreeDepth(state.treeNodes[id].left), getTreeDepth(state.treeNodes[id].right));
    }

    function layoutTree() {
        state.nodePositions = {};
        if (state.rootId === -1) return;
        const depth = getTreeDepth(state.rootId);
        const initialSpread = Math.pow(2, Math.min(depth - 1, 8)) * 30;

        function layout(id, x, y, spread) {
            if (id === -1 || !state.treeNodes[id]) return;
            state.nodePositions[id] = { x, y };
            const childSpread = Math.max(spread / 2, MIN_X_GAP);
            layout(state.treeNodes[id].left, x - spread, y + VERTICAL_GAP, childSpread);
            layout(state.treeNodes[id].right, x + spread, y + VERTICAL_GAP, childSpread);
        }

        layout(state.rootId, 0, 0, initialSpread);
    }

    function drawEdge(from, to, color, width) {
        const angle = Math.atan2(to.y - from.y, to.x - from.x);
        const sx = from.x + NODE_RADIUS * Math.cos(angle);
        const sy = from.y + NODE_RADIUS * Math.sin(angle);
        const ex = to.x - NODE_RADIUS * Math.cos(angle);
        const ey = to.y - NODE_RADIUS * Math.sin(angle);

        ctx.beginPath();
        ctx.moveTo(sx, sy);
        ctx.lineTo(ex, ey);
        ctx.strokeStyle = color;
        ctx.lineWidth = width;
        ctx.lineCap = "round";
        ctx.stroke();
    }

    function drawNode(x, y, value, color) {
        ctx.save();
        ctx.shadowColor = "rgba(15, 23, 42, 0.16)";
        ctx.shadowBlur = 8;
        ctx.shadowOffsetY = 2;
        ctx.beginPath();
        ctx.arc(x, y, NODE_RADIUS, 0, Math.PI * 2);
        ctx.fillStyle = color;
        ctx.fill();
        ctx.restore();

        ctx.beginPath();
        ctx.arc(x, y, NODE_RADIUS, 0, Math.PI * 2);
        ctx.strokeStyle = "rgba(15, 23, 42, 0.08)";
        ctx.lineWidth = 1.5;
        ctx.stroke();

        ctx.fillStyle = "#fff";
        ctx.font = "600 14px Inter, Segoe UI, sans-serif";
        ctx.textAlign = "center";
        ctx.textBaseline = "middle";
        ctx.fillText(String(value), x, y);
    }

    function drawTree() {
        const dpr = window.devicePixelRatio || 1;
        const displayW = dom.canvasContainer.getBoundingClientRect().width;
        const displayH = dom.canvasContainer.getBoundingClientRect().height;
        ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
        ctx.clearRect(0, 0, displayW, displayH);
        if (state.rootId === -1) return;

        ctx.save();
        ctx.scale(cam.scale, cam.scale);
        ctx.translate(cam.x, cam.y);

        for (const idStr of Object.keys(state.nodePositions)) {
            const id = Number.parseInt(idStr, 10);
            const node = state.treeNodes[id];
            const pos = state.nodePositions[id];
            if (!node || !pos) continue;

            if (node.left !== -1 && state.nodePositions[node.left]) {
                const key = `${id}-${node.left}`;
                const highlighted = state.edgeHighlights[key] !== undefined;
                const color = highlighted ? VIZ_COLORS[state.edgeHighlights[key]] : DEFAULT_EDGE_COLOR;
                drawEdge(pos, state.nodePositions[node.left], color, (highlighted ? 3 : 1.5) / cam.scale);
            }
            if (node.right !== -1 && state.nodePositions[node.right]) {
                const key = `${id}-${node.right}`;
                const highlighted = state.edgeHighlights[key] !== undefined;
                const color = highlighted ? VIZ_COLORS[state.edgeHighlights[key]] : DEFAULT_EDGE_COLOR;
                drawEdge(pos, state.nodePositions[node.right], color, (highlighted ? 3 : 1.5) / cam.scale);
            }
        }

        for (const idStr of Object.keys(state.nodePositions)) {
            const id = Number.parseInt(idStr, 10);
            const node = state.treeNodes[id];
            const pos = state.nodePositions[id];
            if (!node || !pos) continue;
            const color = state.nodeHighlights[id] !== undefined ? VIZ_COLORS[state.nodeHighlights[id]] : DEFAULT_NODE_COLOR;
            drawNode(pos.x, pos.y, node.value, color);
        }

        ctx.restore();
    }

    function bindCanvasGestures() {
        dom.canvas.addEventListener("mousedown", (e) => {
            isDragging = true;
            dragStart = { x: e.clientX, y: e.clientY };
            camStart = { x: cam.x, y: cam.y };
            dom.canvasContainer.classList.add("dragging");
        });

        window.addEventListener("mousemove", (e) => {
            if (!isDragging) return;
            cam.x = camStart.x + (e.clientX - dragStart.x) / cam.scale;
            cam.y = camStart.y + (e.clientY - dragStart.y) / cam.scale;
            drawTree();
        });

        window.addEventListener("mouseup", () => {
            isDragging = false;
            dom.canvasContainer.classList.remove("dragging");
        });

        dom.canvas.addEventListener(
            "wheel",
            (e) => {
                e.preventDefault();
                const rect = dom.canvasContainer.getBoundingClientRect();
                zoomAt(e.clientX - rect.left, e.clientY - rect.top, e.deltaY < 0 ? 1.12 : 1 / 1.12);
            },
            { passive: false },
        );

        dom.canvas.addEventListener(
            "touchstart",
            (e) => {
                if (e.touches.length !== 1) return;
                lastTouch = { x: e.touches[0].clientX, y: e.touches[0].clientY };
                camStart = { x: cam.x, y: cam.y };
            },
            { passive: true },
        );

        dom.canvas.addEventListener(
            "touchmove",
            (e) => {
                if (e.touches.length !== 1 || !lastTouch) return;
                e.preventDefault();
                cam.x = camStart.x + (e.touches[0].clientX - lastTouch.x) / cam.scale;
                cam.y = camStart.y + (e.touches[0].clientY - lastTouch.y) / cam.scale;
                drawTree();
            },
            { passive: false },
        );

        dom.canvas.addEventListener("touchend", () => {
            lastTouch = null;
        });

        window.addEventListener("resize", resizeCanvas);
    }

    return { resizeCanvas, drawTree, layoutTree, fitTreeToView, spreadTree, zoomAt, bindCanvasGestures };
}
