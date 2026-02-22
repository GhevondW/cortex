/**
 * Canvas Renderer Module
 * 
 * Handles all canvas drawing operations for the tree visualization.
 * Responsible for rendering nodes, edges, and visual effects.
 */

import AppState from './state.js';

// Constants
const NODE_RADIUS = 22;
const DEFAULT_NODE_COLOR = '#1976D2';
const DEFAULT_EDGE_COLOR = '#bdbdbd';

const VIZ_COLORS = {
  0: '#1976D2', // DEFAULT (blue)
  1: '#FFA000', // COMPARING (amber)
  2: '#F57C00', // VISITING (orange)
  3: '#2E7D32', // FOUND (green)
  4: '#43A047', // INSERTED (green)
  5: '#D32F2F', // BACKTRACK (red)
  6: '#0288D1', // QUEUED (light blue)
  7: '#7B1FA2', // PATH (purple)
  8: '#C62828', // DELETE (dark red)
};

/**
 * Get color for a visualization state
 */
function getColor(stateIndex) {
  return VIZ_COLORS[stateIndex] || DEFAULT_NODE_COLOR;
}

/**
 * Resize canvas for HiDPI displays
 */
function resizeCanvas(canvas, container) {
  const dpr = window.devicePixelRatio || 1;
  const rect = container.getBoundingClientRect();
  
  canvas.width = rect.width * dpr;
  canvas.height = rect.height * dpr;
  canvas.style.width = `${rect.width}px`;
  canvas.style.height = `${rect.height}px`;
  
  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  
  return { width: rect.width, height: rect.height };
}

/**
 * Clear the canvas
 */
function clearCanvas(ctx, width, height) {
  ctx.clearRect(0, 0, width, height);
}

/**
 * Draw an edge between two points
 */
function drawEdge(ctx, from, to, color, width, scale) {
  const angle = Math.atan2(to.y - from.y, to.x - from.x);
  const sx = from.x + NODE_RADIUS * Math.cos(angle);
  const sy = from.y + NODE_RADIUS * Math.sin(angle);
  const ex = to.x - NODE_RADIUS * Math.cos(angle);
  const ey = to.y - NODE_RADIUS * Math.sin(angle);
  
  ctx.beginPath();
  ctx.moveTo(sx, sy);
  ctx.lineTo(ex, ey);
  ctx.strokeStyle = color;
  ctx.lineWidth = width / scale;
  ctx.lineCap = 'round';
  ctx.stroke();
}

/**
 * Draw a node circle with value
 */
function drawNode(ctx, x, y, value, color) {
  // Save context for shadow
  ctx.save();
  ctx.shadowColor = 'rgba(0, 0, 0, 0.18)';
  ctx.shadowBlur = 8;
  ctx.shadowOffsetX = 0;
  ctx.shadowOffsetY = 2;
  
  // Draw node circle
  ctx.beginPath();
  ctx.arc(x, y, NODE_RADIUS, 0, Math.PI * 2);
  ctx.fillStyle = color;
  ctx.fill();
  ctx.restore();
  
  // Draw border
  ctx.beginPath();
  ctx.arc(x, y, NODE_RADIUS, 0, Math.PI * 2);
  ctx.strokeStyle = 'rgba(0, 0, 0, 0.08)';
  ctx.lineWidth = 1.5;
  ctx.stroke();
  
  // Draw value text
  ctx.fillStyle = '#ffffff';
  ctx.font = "bold 14px -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif";
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  ctx.fillText(String(value), x, y);
}

/**
 * Main render function - draws the entire tree
 */
function render(canvas, container) {
  const ctx = canvas.getContext('2d');
  const { width, height } = resizeCanvas(canvas, container);
  
  clearCanvas(ctx, width, height);
  
  const rootId = AppState.getRootId();
  if (rootId === -1) return;
  
  const nodePositions = AppState.getNodePositions();
  const treeNodes = AppState.getTreeNodes();
  const nodeHighlights = AppState.getNodeHighlights();
  const edgeHighlights = AppState.getEdgeHighlights();
  const camera = AppState.getCamera();
  
  // Apply camera transform
  ctx.save();
  ctx.scale(camera.scale, camera.scale);
  ctx.translate(camera.x, camera.y);
  
  // Draw edges first (underneath nodes)
  for (const idStr of Object.keys(nodePositions)) {
    const id = parseInt(idStr);
    const node = treeNodes[id];
    const pos = nodePositions[id];
    
    if (!node || !pos) continue;
    
    // Draw left edge
    if (node.left !== -1 && nodePositions[node.left]) {
      const key = `${id}-${node.left}`;
      const highlightColor = edgeHighlights[key];
      const color = highlightColor !== undefined 
        ? getColor(highlightColor) 
        : DEFAULT_EDGE_COLOR;
      const width = highlightColor !== undefined ? 3 : 1.5;
      drawEdge(ctx, pos, nodePositions[node.left], color, width, camera.scale);
    }
    
    // Draw right edge
    if (node.right !== -1 && nodePositions[node.right]) {
      const key = `${id}-${node.right}`;
      const highlightColor = edgeHighlights[key];
      const color = highlightColor !== undefined 
        ? getColor(highlightColor) 
        : DEFAULT_EDGE_COLOR;
      const width = highlightColor !== undefined ? 3 : 1.5;
      drawEdge(ctx, pos, nodePositions[node.right], color, width, camera.scale);
    }
  }
  
  // Draw nodes on top
  for (const idStr of Object.keys(nodePositions)) {
    const id = parseInt(idStr);
    const node = treeNodes[id];
    const pos = nodePositions[id];
    
    if (!node || !pos) continue;
    
    const highlightColor = nodeHighlights[id];
    const color = highlightColor !== undefined 
      ? getColor(highlightColor) 
      : DEFAULT_NODE_COLOR;
    
    drawNode(ctx, pos.x, pos.y, node.value, color);
  }
  
  ctx.restore();
}

/**
 * Show empty state message
 */
function showEmptyMessage(emptyMsgElement, show) {
  if (emptyMsgElement) {
    emptyMsgElement.style.display = show ? 'flex' : 'none';
  }
}

export {
  NODE_RADIUS,
  resizeCanvas,
  render,
  showEmptyMessage
};

export default {
  NODE_RADIUS,
  resizeCanvas,
  render,
  showEmptyMessage
};
