/**
 * Tree Layout Module
 * 
 * Handles tree layout algorithms for positioning nodes.
 * Supports recursive tree layout with configurable spacing.
 */

import AppState from './state.js';

// Constants
const VERTICAL_GAP = 68;
const MIN_X_GAP = 32;

/**
 * Calculate the depth of a tree from a given node
 */
function getTreeDepth(nodeId) {
  const treeNodes = AppState.getTreeNodes();
  
  if (nodeId === -1 || !treeNodes[nodeId]) {
    return 0;
  }
  
  const node = treeNodes[nodeId];
  return 1 + Math.max(
    getTreeDepth(node.left),
    getTreeDepth(node.right)
  );
}

/**
 * Layout the tree with proper spacing
 * Uses a recursive approach with exponential horizontal spacing based on depth
 */
function layout() {
  const rootId = AppState.getRootId();
  if (rootId === -1) {
    return;
  }
  
  const depth = getTreeDepth(rootId);
  // Use generous spacing that grows with tree depth
  const initialSpread = Math.pow(2, Math.min(depth - 1, 8)) * 30;
  
  function layoutNode(nodeId, x, y, spread) {
    if (nodeId === -1) {
      return;
    }
    
    const treeNodes = AppState.getTreeNodes();
    if (!treeNodes[nodeId]) {
      return;
    }
    
    // Position current node
    AppState.setNodePosition(nodeId, x, y);
    
    // Calculate child spread (minimum gap enforced)
    const childSpread = Math.max(spread / 2, MIN_X_GAP);
    
    const node = treeNodes[nodeId];
    
    // Layout children
    layoutNode(node.left, x - spread, y + VERTICAL_GAP, childSpread);
    layoutNode(node.right, x + spread, y + VERTICAL_GAP, childSpread);
  }
  
  // Start layout from root at origin
  layoutNode(rootId, 0, 0, initialSpread);
}

/**
 * Position a new node relative to its parent
 * Used for incremental layout when adding nodes
 */
function positionNewNode(nodeId, parentId, isLeft) {
  const positions = AppState.getNodePositions();
  const treeNodes = AppState.getTreeNodes();
  
  if (parentId === -1) {
    // Root node - place at origin
    AppState.setNodePosition(nodeId, 0, 0);
    return;
  }
  
  const parentPos = positions[parentId];
  if (!parentPos) {
    return;
  }
  
  // Calculate spread based on grandparent distance
  const grandparentId = treeNodes[parentId]?.parent ?? -1;
  let spread;
  
  if (grandparentId !== -1 && positions[grandparentId]) {
    spread = Math.abs(parentPos.x - positions[grandparentId].x) / 2;
  } else {
    spread = VERTICAL_GAP; // Root's direct children
  }
  
  spread = Math.max(spread, MIN_X_GAP);
  
  const newX = parentPos.x + (isLeft ? -spread : spread);
  const newY = parentPos.y + VERTICAL_GAP;
  
  AppState.setNodePosition(nodeId, newX, newY);
}

export {
  getTreeDepth,
  layout,
  positionNewNode,
  VERTICAL_GAP,
  MIN_X_GAP
};

export default {
  getTreeDepth,
  layout,
  positionNewNode,
  VERTICAL_GAP,
  MIN_X_GAP
};
