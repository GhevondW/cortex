/**
 * Main Application Module
 * 
 * Orchestrates all modules and handles the main application lifecycle.
 * This is the entry point for the AlgoViz application.
 */

import AppState from './state.js';
import { render, showEmptyMessage } from './renderer.js';
import { init as initCamera, fitToView } from './camera.js';
import { layout } from './layout.js';
import {
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
} from './wasm-bridge.js';
import UIController from './ui-controller.js';

// Playback timer
let playTimer = null;
let isInitialized = false;

/**
 * Initialize the application
 */
function init() {
  // Prevent multiple initializations
  if (isInitialized) return;
  isInitialized = true;

  // Initialize UI elements
  UIController.initElements();

  // Setup WASM bridge with callbacks
  setupBridge(render, {
    onTraversalUpdate: handleTraversalUpdate,
    onStatusUpdate: handleStatusUpdate,
    onStatsUpdate: handleStatsUpdate,
    onAlgorithmComplete: handleAlgorithmComplete
  });

  // Setup UI event listeners
  UIController.setupEventListeners({
    onRun: handleRun,
    onStep: handleStep,
    onPause: handlePause,
    onReset: handleReset,
    onSpeedChange: handleSpeedChange,
    onAlgorithmChange: handleAlgorithmChange,
    onLoadPreset: handleLoadPreset,
    onAddNode: handleAddNode,
    onRandom: handleRandom,
    onClear: handleClear
  });

  // Initialize camera controls
  const canvas = UIController.getElement('canvas');
  const container = UIController.getElement('canvasContainer');
  initCamera(canvas, container, render);

  // Initialize speed display
  UIController.updateSpeedDisplay();

  // Initialize WASM module
  initModule(onModuleReady);

  // Handle window resize
  window.addEventListener('resize', handleResize);
}

/**
 * Called when WASM module is ready
 */
function onModuleReady() {
  // Load default preset (balanced tree)
  buildPresetTree(0);
  
  // Sync and layout tree
  const { rootId } = syncTreeFromWasm();
  showEmptyMessage(UIController.getElement('emptyMsg'), rootId === -1);
  
  // Initial render
  handleResize();
  
  // Update UI
  UIController.updateStatus('Ready. Select an algorithm and click Run.');
  UIController.updateButtonStates();
}

/**
 * Handle window resize
 */
function handleResize() {
  const canvas = UIController.getElement('canvas');
  const container = UIController.getElement('canvasContainer');

  layout();
  fitToView();
  render(canvas, container);
}

/**
 * Render the current tree state
 */
function renderTree() {
  const canvas = UIController.getElement('canvas');
  const container = UIController.getElement('canvasContainer');
  const rootId = AppState.getRootId();
  
  showEmptyMessage(UIController.getElement('emptyMsg'), rootId === -1);
  render(canvas, container);
}

/**
 * ================================================================
 * Event Handlers
 * ================================================================
 */

function handleRun() {
  if (AppState.isAlgorithmRunning()) return;

  const algoId = parseInt(UIController.getElement('algorithmSelect').value);
  const value = parseInt(UIController.getElement('valueInput').value) || 0;

  AppState.setRunning(true);
  AppState.setPaused(false);
  AppState.setStepCount(0);
  AppState.setComparisons(0);
  
  UIController.updateStatsDisplay();
  UIController.updateButtonStates();

  setOperationValue(value);
  
  // Start algorithm asynchronously
  startAlgorithm(algoId).then(() => {
    AppState.setStepCount(AppState.getStepCount() + 1);
    UIController.updateStatsDisplay();

    if (!isAlgorithmDone()) {
      scheduleStep();
    }
  });
}

function handleStep() {
  if (!AppState.isAlgorithmRunning()) {
    // Start in paused mode
    AppState.setRunning(true);
    AppState.setPaused(true);
    UIController.updateButtonStates();
    return;
  }

  if (isAlgorithmDone()) return;

  stepAlgorithm().then(() => {
    AppState.setStepCount(AppState.getStepCount() + 1);
    UIController.updateStatsDisplay();
  });
}

function handlePause() {
  if (!AppState.isAlgorithmRunning()) return;

  const wasPaused = AppState.isAlgorithmPaused();
  AppState.setPaused(!wasPaused);
  UIController.updateButtonStates();

  if (!wasPaused) {
    // Now paused - cancel timer
    if (playTimer) {
      clearTimeout(playTimer);
      playTimer = null;
    }
  } else {
    // Now running - resume scheduling
    scheduleStep();
  }
}

function handleReset() {
  if (playTimer) {
    clearTimeout(playTimer);
    playTimer = null;
  }

  AppState.resetPlaybackState();
  resetAlgorithm();
  
  syncTreeFromWasm();
  layout();
  fitToView(render);
  renderTree();
  
  UIController.updateStatus('Reset. Ready.');
  UIController.updateButtonStates();
}

function handleSpeedChange() {
  UIController.updateSpeedDisplay();
}

function handleAlgorithmChange() {
  UIController.updateButtonStates();
}

function handleLoadPreset() {
  const presetIndex = parseInt(UIController.getElement('presetSelect').value);
  
  AppState.resetPlaybackState();
  buildPresetTree(presetIndex);
  
  const { rootId } = syncTreeFromWasm();
  showEmptyMessage(UIController.getElement('emptyMsg'), rootId === -1);
  
  layout();
  fitToView(render);
  renderTree();
  
  UIController.resetInputs();
  UIController.updateStatus('Preset loaded. Ready.');
}

function handleAddNode() {
  const value = parseInt(UIController.getElement('addValueInput').value);
  if (isNaN(value)) return;

  AppState.resetPlaybackState();
  addNode(value);
  
  syncTreeFromWasm();
  layout();
  fitToView(render);
  renderTree();
  
  UIController.getElement('addValueInput').value = '';
  UIController.updateStatus(`Added ${value}. Ready.`);
}

function handleRandom() {
  AppState.resetPlaybackState();
  clearTree();
  
  const used = new Set();
  for (let i = 0; i < 8; i++) {
    let v;
    do {
      v = Math.floor(Math.random() * 95) + 5;
    } while (used.has(v));
    used.add(v);
    addNode(v);
  }
  
  const { rootId } = syncTreeFromWasm();
  showEmptyMessage(UIController.getElement('emptyMsg'), rootId === -1);
  
  layout();
  fitToView(render);
  renderTree();
  
  UIController.resetInputs();
  UIController.updateStatus('Random tree generated. Ready.');
}

function handleClear() {
  AppState.resetPlaybackState();
  clearTree();
  
  syncTreeFromWasm();
  layout();
  fitToView(render);
  renderTree();
  
  UIController.resetInputs();
  UIController.updateStatus('Tree cleared. Ready.');
}

/**
 * ================================================================
 * Callback Handlers (from WASM bridge)
 * ================================================================
 */

function handleTraversalUpdate(value) {
  UIController.updateTraversalOutput(value);
}

function handleStatusUpdate(message) {
  UIController.updateStatus(message);
}

function handleStatsUpdate() {
  UIController.updateStatsDisplay();
}

function handleAlgorithmComplete() {
  AppState.setRunning(false);
  AppState.setPaused(false);
  
  if (playTimer) {
    clearTimeout(playTimer);
    playTimer = null;
  }
  
  UIController.updateButtonStates();
}

/**
 * ================================================================
 * Playback Scheduling
 * ================================================================
 */

function scheduleStep() {
  if (!AppState.isAlgorithmRunning() || AppState.isAlgorithmPaused()) return;

  playTimer = setTimeout(async () => {
    if (!AppState.isAlgorithmRunning() || AppState.isAlgorithmPaused()) return;

    await stepAlgorithm();
    AppState.setStepCount(AppState.getStepCount() + 1);
    UIController.updateStatsDisplay();

    if (!isAlgorithmDone()) {
      scheduleStep();
    }
  }, AppState.getCurrentSpeed());
}

// Export the init function as the main entry point
export { init };

export default { init };

// Auto-initialize when module loads
init();
