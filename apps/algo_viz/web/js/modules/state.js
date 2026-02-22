/**
 * State Management Module
 * 
 * Centralized state management for the AlgoViz application.
 * Handles tree data, visualization state, and playback state.
 */

export const AppState = (() => {
  // Tree structure state
  let treeNodes = {};       // id -> { value, left, right, parent }
  let rootId = -1;
  let nodePositions = {};   // id -> { x, y }
  let nodeHighlights = {};  // id -> color_int
  let edgeHighlights = {};  // "from-to" -> color_int

  // Playback state
  let isRunning = false;
  let isPaused = false;
  let isReady = false;
  let stepCount = 0;
  let comparisons = 0;
  let currentSpeed = 300;

  // Camera state
  let camera = { x: 0, y: 0, scale: 1 };

  /**
   * Getters for tree data
   */
  function getTreeNodes() { return { ...treeNodes }; }
  function getRootId() { return rootId; }
  function getNodePositions() { return { ...nodePositions }; }
  function getNodeHighlights() { return { ...nodeHighlights }; }
  function getEdgeHighlights() { return { ...edgeHighlights }; }
  function getCamera() { return { ...camera }; }

  /**
   * Getters for playback state
   */
  function isAlgorithmRunning() { return isRunning; }
  function isAlgorithmPaused() { return isPaused; }
  function isModuleReady() { return isReady; }
  function getStepCount() { return stepCount; }
  function getComparisons() { return comparisons; }
  function getCurrentSpeed() { return currentSpeed; }

  /**
   * Setters for module readiness
   */
  function setModuleReady(ready) { isReady = ready; }

  /**
   * Tree synchronization from WASM
   */
  function syncTreeFromWasm(Module) {
    treeNodes = {};
    rootId = Module.ccall('get_root_id', 'number', [], []);
    const count = Module.ccall('get_node_count', 'number', [], []);
    
    for (let i = 0; i < count; i++) {
      const left = Module.ccall('get_node_left', 'number', ['number'], [i]);
      const right = Module.ccall('get_node_right', 'number', ['number'], [i]);
      const parent = Module.ccall('get_node_parent', 'number', ['number'], [i]);
      const value = Module.ccall('get_node_value', 'number', ['number'], [i]);
      treeNodes[i] = { value, left, right, parent };
    }
    
    return { rootId, nodeCount: Object.keys(treeNodes).length };
  }

  /**
   * Update tree structure
   */
  function setTreeNodes(nodes) { treeNodes = nodes; }
  function setRootId(id) { rootId = id; }
  function setNodePositions(positions) { nodePositions = positions; }

  /**
   * Update node position (for incremental layout)
   */
  function setNodePosition(nodeId, x, y) {
    nodePositions[nodeId] = { x, y };
  }

  /**
   * Highlight management
   */
  function highlightNode(nodeId, color) {
    nodeHighlights[nodeId] = color;
  }

  function unhighlightAllNodes() {
    nodeHighlights = {};
  }

  function highlightEdge(fromId, toId, color) {
    edgeHighlights[`${fromId}-${toId}`] = color;
  }

  function unhighlightAllEdges() {
    edgeHighlights = {};
  }

  /**
   * Update node value
   */
  function updateNodeValue(nodeId, newValue) {
    if (treeNodes[nodeId]) {
      treeNodes[nodeId].value = newValue;
    }
  }

  /**
   * Add node to tree
   */
  function addNode(nodeId, value, parentId, isLeft) {
    treeNodes[nodeId] = { value, left: -1, right: -1, parent: parentId };
    
    if (parentId !== -1 && treeNodes[parentId]) {
      if (isLeft) {
        treeNodes[parentId].left = nodeId;
      } else {
        treeNodes[parentId].right = nodeId;
      }
    }
    
    if (rootId === -1) {
      rootId = nodeId;
    }
  }

  /**
   * Remove node from tree
   */
  function removeNode(nodeId) {
    delete nodeHighlights[nodeId];
    delete nodePositions[nodeId];
    // Note: Full structure sync should be done via syncTreeFromWasm
  }

  /**
   * Playback state management
   */
  function setRunning(running) { isRunning = running; }
  function setPaused(paused) { isPaused = paused; }
  function setStepCount(count) { stepCount = count; }
  function setComparisons(count) { comparisons = count; }
  function setCurrentSpeed(speed) { currentSpeed = speed; }

  /**
   * Reset playback state
   */
  function resetPlaybackState() {
    isRunning = false;
    isPaused = false;
    stepCount = 0;
    comparisons = 0;
    nodeHighlights = {};
    edgeHighlights = {};
  }

  /**
   * Camera state management
   */
  function setCamera(x, y, scale) {
    camera = { x, y, scale };
  }

  function updateCamera(deltaX, deltaY) {
    camera.x += deltaX;
    camera.y += deltaY;
  }

  function zoomCamera(factor, mouseX, mouseY, canvasWidth, canvasHeight) {
    const wx = mouseX / camera.scale - camera.x;
    const wy = mouseY / camera.scale - camera.y;
    
    camera.scale = Math.min(Math.max(camera.scale * factor, 0.15), 4);
    
    camera.x = mouseX / camera.scale - wx;
    camera.y = mouseY / camera.scale - wy;
  }

  function resetCamera() {
    camera = { x: 0, y: 0, scale: 1 };
  }

  /**
   * Clear all tree data
   */
  function clearTree() {
    treeNodes = {};
    rootId = -1;
    nodePositions = {};
    nodeHighlights = {};
    edgeHighlights = {};
  }

  return {
    // Getters
    getTreeNodes,
    getRootId,
    getNodePositions,
    getNodeHighlights,
    getEdgeHighlights,
    getCamera,
    isAlgorithmRunning,
    isAlgorithmPaused,
    isModuleReady,
    getStepCount,
    getComparisons,
    getCurrentSpeed,
    
    // Setters
    setModuleReady,
    setTreeNodes,
    setRootId,
    setNodePositions,
    setNodePosition,
    setRunning,
    setPaused,
    setStepCount,
    setComparisons,
    setCurrentSpeed,
    
    // Tree operations
    syncTreeFromWasm,
    highlightNode,
    unhighlightAllNodes,
    highlightEdge,
    unhighlightAllEdges,
    updateNodeValue,
    addNode,
    removeNode,
    resetPlaybackState,
    
    // Camera operations
    setCamera,
    updateCamera,
    zoomCamera,
    resetCamera,
    
    // Clear
    clearTree
  };
})();

export default AppState;
