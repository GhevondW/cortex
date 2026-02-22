/**
 * UI Controller Module
 * 
 * Handles all UI interactions, button states, and user input.
 * Manages the connection between user actions and application logic.
 */

import AppState from './state.js';

// DOM Element references
let elements = {};

/**
 * Initialize references to all DOM elements
 */
function initElements() {
  elements = {
    // Canvas elements
    canvas: document.getElementById('treeCanvas'),
    canvasContainer: document.getElementById('canvasContainer'),
    emptyMsg: document.getElementById('emptyMsg'),
    traversalOutput: document.getElementById('traversalOutput'),
    statusText: document.getElementById('statusText'),
    comparisonsEl: document.getElementById('comparisonsEl'),
    stepsEl: document.getElementById('stepsEl'),

    // Algorithm controls
    algorithmSelect: document.getElementById('algorithmSelect'),
    valueInput: document.getElementById('valueInput'),
    valueInputGroup: document.getElementById('valueInputGroup'),
    runBtn: document.getElementById('runBtn'),
    stepBtn: document.getElementById('stepBtn'),
    pauseBtn: document.getElementById('pauseBtn'),
    resetBtn: document.getElementById('resetBtn'),
    speedSlider: document.getElementById('speedSlider'),
    speedLabel: document.getElementById('speedLabel'),

    // Tree controls
    presetSelect: document.getElementById('presetSelect'),
    loadPresetBtn: document.getElementById('loadPresetBtn'),
    addValueInput: document.getElementById('addValueInput'),
    addNodeBtn: document.getElementById('addNodeBtn'),
    randomBtn: document.getElementById('randomBtn'),
    clearBtn: document.getElementById('clearBtn')
  };

  return elements;
}

/**
 * Get element by key
 */
function getElement(key) {
  return elements[key];
}

/**
 * Get all elements
 */
function getElements() {
  return { ...elements };
}

/**
 * Update button states based on current application state
 */
function updateButtonStates() {
  const algoIndex = parseInt(elements.algorithmSelect.value);
  const algoNeedsValue = algoIndex <= 2; // Insert, Delete, Find need a value
  const isRunning = AppState.isAlgorithmRunning();
  const isPaused = AppState.isAlgorithmPaused();
  const isReady = AppState.isModuleReady();

  // Show/hide value input based on algorithm
  elements.valueInputGroup.style.display = algoNeedsValue ? '' : 'none';

  if (isRunning) {
    // Running state
    elements.runBtn.disabled = true;
    elements.stepBtn.disabled = isPaused ? false : true;
    elements.pauseBtn.disabled = false;
    elements.pauseBtn.textContent = isPaused ? '▶ Resume' : '⏸ Pause';
    elements.resetBtn.disabled = false;

    elements.loadPresetBtn.disabled = true;
    elements.addNodeBtn.disabled = true;
    elements.randomBtn.disabled = true;
    elements.clearBtn.disabled = true;
  } else {
    // Idle state
    elements.runBtn.disabled = !isReady;
    elements.stepBtn.disabled = !isReady;
    elements.pauseBtn.disabled = true;
    elements.pauseBtn.textContent = '⏸ Pause';
    elements.resetBtn.disabled = true;

    elements.loadPresetBtn.disabled = !isReady;
    elements.addNodeBtn.disabled = !isReady;
    elements.randomBtn.disabled = !isReady;
    elements.clearBtn.disabled = !isReady;
  }
}

/**
 * Update speed display label
 */
function updateSpeedDisplay() {
  const sliderValue = parseInt(elements.speedSlider.value);
  const delay = calculateDelay(sliderValue);
  
  AppState.setCurrentSpeed(delay);
  
  elements.speedLabel.textContent = delay >= 1000
    ? `${(delay / 1000).toFixed(1)}s`
    : `${delay}ms`;
}

/**
 * Calculate delay from slider value (exponential mapping)
 */
function calculateDelay(sliderValue) {
  const minDelay = 40;
  const maxDelay = 2000;
  const t = (sliderValue - 1) / 99;
  return Math.round(maxDelay * Math.pow(minDelay / maxDelay, t));
}

/**
 * Update stats display (comparisons and steps)
 */
function updateStatsDisplay() {
  elements.comparisonsEl.textContent = AppState.getComparisons();
  elements.stepsEl.textContent = AppState.getStepCount();
}

/**
 * Update traversal output display
 */
function updateTraversalOutput(value) {
  if (value === null) {
    // Clear output
    elements.traversalOutput.textContent = '—';
  } else {
    const current = elements.traversalOutput.textContent;
    if (current === '—' || current === '') {
      elements.traversalOutput.textContent = String(value);
    } else {
      elements.traversalOutput.textContent = `${current}  →  ${value}`;
    }
  }
}

/**
 * Update status message
 */
function updateStatus(message) {
  elements.statusText.textContent = message;
}

/**
 * Update empty message visibility
 */
function updateEmptyMessage(show) {
  elements.emptyMsg.style.display = show ? 'flex' : 'none';
}

/**
 * Setup event listeners for all controls
 */
function setupEventListeners(callbacks) {
  const {
    onRun,
    onStep,
    onPause,
    onReset,
    onSpeedChange,
    onAlgorithmChange,
    onLoadPreset,
    onAddNode,
    onRandom,
    onClear
  } = callbacks;

  // Playback controls
  elements.runBtn.addEventListener('click', onRun);
  elements.stepBtn.addEventListener('click', onStep);
  elements.pauseBtn.addEventListener('click', onPause);
  elements.resetBtn.addEventListener('click', onReset);

  // Speed control
  elements.speedSlider.addEventListener('input', onSpeedChange);

  // Algorithm selection
  elements.algorithmSelect.addEventListener('change', () => {
    onAlgorithmChange?.();
    updateButtonStates();
  });

  // Tree controls
  elements.loadPresetBtn.addEventListener('click', onLoadPreset);
  elements.addNodeBtn.addEventListener('click', onAddNode);
  elements.randomBtn.addEventListener('click', onRandom);
  elements.clearBtn.addEventListener('click', onClear);

  // Enter key for add value input
  elements.addValueInput.addEventListener('keydown', function(e) {
    if (e.key === 'Enter') {
      elements.addNodeBtn.click();
    }
  });
}

/**
 * Reset form inputs
 */
function resetInputs() {
  elements.addValueInput.value = '';
  elements.traversalOutput.textContent = '—';
}

export {
  initElements,
  getElement,
  getElements,
  updateButtonStates,
  updateSpeedDisplay,
  updateStatsDisplay,
  updateTraversalOutput,
  updateStatus,
  updateEmptyMessage,
  setupEventListeners,
  resetInputs,
  calculateDelay
};

export default {
  initElements,
  getElement,
  getElements,
  updateButtonStates,
  updateSpeedDisplay,
  updateStatsDisplay,
  updateTraversalOutput,
  updateStatus,
  updateEmptyMessage,
  setupEventListeners,
  resetInputs,
  calculateDelay
};
