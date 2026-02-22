/**
 * WASM Bridge Module
 * 
 * Handles communication between JavaScript and the WebAssembly module.
 * Sets up callback functions that the WASM module can call.
 */

import AppState from './state.js';
import { render } from './renderer.js';
import { layout, positionNewNode } from './layout.js';
import { fitToView } from './camera.js';
import UIController from './ui-controller.js';

// Access the global Module object created by the WASM loader
const getModule = () => window.Module || globalThis.Module;

// Store canvas and container for render callbacks
let canvasElement = null;
let containerElement = null;

/**
 * Internal render helper
 */
function doRender() {
  if (!canvasElement || !containerElement) return;
  render(canvasElement, containerElement);
}

/**
 * Setup WASM bridge callbacks
 * These functions are called from the C++/WASM side
 */
function setupBridge(renderCallback, uiCallbacks) {
  // Get canvas and container for rendering
  canvasElement = UIController.getElement('canvas');
  containerElement = UIController.getElement('canvasContainer');
  
  const {
    onTraversalUpdate,
    onStatusUpdate,
    onStatsUpdate,
    onAlgorithmComplete
  } = uiCallbacks;

  /**
   * Called when a node is visited during algorithm execution
   */
  window.onTreeVisit = function(nodeId, color) {
    AppState.highlightNode(nodeId, color);

    if (color === 1) { // COMPARING
      const comparisons = AppState.getComparisons() + 1;
      AppState.setComparisons(comparisons);
      onStatsUpdate?.();
    }

    doRender();
  };

  /**
   * Called to clear all node highlights
   */
  window.onTreeUnvisitAll = function() {
    AppState.unhighlightAllNodes();
    doRender();
  };

  /**
   * Called when a new node is added to the tree
   */
  window.onTreeAddNode = function(nodeId, value, parentId, isLeft) {
    // Update state
    AppState.addNode(nodeId, value, parentId, isLeft);

    // Position the new node
    positionNewNode(nodeId, parentId, isLeft);

    doRender();
  };

  /**
   * Called when a node is removed from the tree
   */
  window.onTreeRemoveNode = function(nodeId) {
    AppState.removeNode(nodeId);
    doRender();
  };

  /**
   * Called when a node's value is updated
   */
  window.onTreeUpdateValue = function(nodeId, newValue) {
    AppState.updateNodeValue(nodeId, newValue);
    doRender();
  };

  /**
   * Called to highlight an edge
   */
  window.onTreeHighlightEdge = function(fromId, toId, color) {
    AppState.highlightEdge(fromId, toId, color);
    doRender();
  };

  /**
   * Called to clear all edge highlights
   */
  window.onTreeUnhighlightEdges = function() {
    AppState.unhighlightAllEdges();
    doRender();
  };

  /**
   * Called to update status message
   */
  window.onTreeLog = function(msg) {
    onStatusUpdate?.(msg);
  };

  /**
   * Called to append a value to the traversal output
   */
  window.onTreeAppendResult = function(value) {
    onTraversalUpdate?.(value);
  };

  /**
   * Called to clear the traversal output
   */
  window.onTreeClearResult = function() {
    onTraversalUpdate?.(null);
  };

  /**
   * Called when algorithm execution is complete
   */
  window.onTreeDone = function() {
    AppState.setRunning(false);
    AppState.setPaused(false);
    onAlgorithmComplete?.();

    // Sync final state without resetting view
    syncTreeFromWasm();
    doRender();
  };
}

/**
 * Sync tree structure from WASM module
 */
function syncTreeFromWasm() {
  return AppState.syncTreeFromWasm(getModule());
}

/**
 * Initialize the WASM module
 */
function initModule(onReady) {
  const mod = getModule();
  mod.onRuntimeInitialized = function() {
    console.log('AlgoViz WASM runtime initialized');
    AppState.setModuleReady(true);
    onReady?.();
  };
}

/**
 * WASM operation wrappers
 */
function buildPresetTree(presetIndex) {
  getModule().ccall('build_preset_tree', null, ['number'], [presetIndex]);
}

function addNode(value) {
  getModule().ccall('add_node', null, ['number'], [value]);
}

function clearTree() {
  getModule().ccall('clear_tree', null, [], []);
}

function setOperationValue(value) {
  getModule().ccall('set_operation_value', null, ['number'], [value]);
}

async function startAlgorithm(algoId) {
  await getModule().ccall('start_algorithm', null, ['number'], [algoId], { async: true });
}

async function stepAlgorithm() {
  await getModule().ccall('step_algorithm', null, [], [], { async: true });
}

function resetAlgorithm() {
  getModule().ccall('reset_algorithm', null, [], []);
}

function isAlgorithmDone() {
  return getModule().ccall('is_algorithm_done', 'number', [], []) === 1;
}

export {
  setupBridge,
  syncTreeFromWasm,
  initModule,
  buildPresetTree,
  addNode,
  clearTree,
  setOperationValue,
  startAlgorithm,
  stepAlgorithm,
  resetAlgorithm,
  isAlgorithmDone
};

export default {
  setupBridge,
  syncTreeFromWasm,
  initModule,
  buildPresetTree,
  addNode,
  clearTree,
  setOperationValue,
  startAlgorithm,
  stepAlgorithm,
  resetAlgorithm,
  isAlgorithmDone
};
