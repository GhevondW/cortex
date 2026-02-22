/**
 * Camera Controller Module
 * 
 * Handles pan, zoom, and fit-to-view functionality for the canvas.
 * Manages user input for mouse and touch interactions.
 */

import AppState from './state.js';
import { render } from './renderer.js';

// Constants
const MIN_SCALE = 0.15;
const MAX_SCALE = 4;
const ZOOM_FACTOR = 1.12;

// Internal drag state
let isDragging = false;
let dragStart = { x: 0, y: 0 };
let cameraStart = { x: 0, y: 0 };

// Touch support
let lastTouch = null;

// Store canvas and container for render callbacks
let canvasElement = null;
let containerElement = null;

/**
 * Internal render helper that calls render with stored elements
 */
function doRender() {
  if (canvasElement && containerElement) {
    render(canvasElement, containerElement);
  }
}

/**
 * Initialize camera controls on the canvas container
 */
function init(canvas, container, renderCallback) {
  canvasElement = canvas;
  containerElement = container;
  setupMouseDrag(canvas, container, renderCallback);
  setupWheelZoom(canvas, container, renderCallback);
  setupTouchDrag(canvas, container, renderCallback);
  setupToolbarButtons(container, renderCallback);
}

/**
 * Setup mouse drag for panning
 */
function setupMouseDrag(canvas, container, renderCallback) {
  canvas.addEventListener('mousedown', function(e) {
    isDragging = true;
    dragStart = { x: e.clientX, y: e.clientY };
    const camera = AppState.getCamera();
    cameraStart = { x: camera.x, y: camera.y };
    container.classList.add('canvas-card__container--dragging');
  });

  window.addEventListener('mousemove', function(e) {
    if (!isDragging) return;

    const camera = AppState.getCamera();
    const newX = cameraStart.x + (e.clientX - dragStart.x) / camera.scale;
    const newY = cameraStart.y + (e.clientY - dragStart.y) / camera.scale;

    AppState.setCamera(newX, newY, camera.scale);
    doRender();
  });

  window.addEventListener('mouseup', function() {
    isDragging = false;
    container.classList.remove('canvas-card__container--dragging');
  });
}

/**
 * Setup wheel event for zooming
 */
function setupWheelZoom(canvas, container, renderCallback) {
  canvas.addEventListener('wheel', function(e) {
    e.preventDefault();
    const rect = container.getBoundingClientRect();
    const mouseX = e.clientX - rect.left;
    const mouseY = e.clientY - rect.top;

    const zoomFactor = e.deltaY < 0 ? ZOOM_FACTOR : 1 / ZOOM_FACTOR;
    zoomAt(mouseX, mouseY, zoomFactor);
  }, { passive: false });
}

/**
 * Setup touch events for mobile pan
 */
function setupTouchDrag(canvas, container, renderCallback) {
  canvas.addEventListener('touchstart', function(e) {
    if (e.touches.length === 1) {
      lastTouch = { 
        x: e.touches[0].clientX, 
        y: e.touches[0].clientY 
      };
      const camera = AppState.getCamera();
      cameraStart = { x: camera.x, y: camera.y };
    }
  }, { passive: true });

  canvas.addEventListener('touchmove', function(e) {
    if (e.touches.length === 1 && lastTouch) {
      e.preventDefault();
      const camera = AppState.getCamera();
      const newX = cameraStart.x + (e.touches[0].clientX - lastTouch.x) / camera.scale;
      const newY = cameraStart.y + (e.touches[0].clientY - lastTouch.y) / camera.scale;

      AppState.setCamera(newX, newY, camera.scale);
      doRender();
    }
  }, { passive: false });

  canvas.addEventListener('touchend', function() {
    lastTouch = null;
  });
}

/**
 * Setup toolbar button handlers
 */
function setupToolbarButtons(container) {
  container.querySelector('#zoomInBtn')?.addEventListener('click', function() {
    const rect = container.getBoundingClientRect();
    zoomAt(rect.width / 2, rect.height / 2, 1.3);
  });

  container.querySelector('#zoomOutBtn')?.addEventListener('click', function() {
    const rect = container.getBoundingClientRect();
    zoomAt(rect.width / 2, rect.height / 2, 1 / 1.3);
  });

  container.querySelector('#fitBtn')?.addEventListener('click', function() {
    fitToView();
  });

  container.querySelector('#spreadHBtn')?.addEventListener('click', function() {
    scaleNodes(1.4, 1);
  });

  container.querySelector('#shrinkHBtn')?.addEventListener('click', function() {
    scaleNodes(1 / 1.4, 1);
  });

  container.querySelector('#spreadVBtn')?.addEventListener('click', function() {
    scaleNodes(1, 1.4);
  });

  container.querySelector('#shrinkVBtn')?.addEventListener('click', function() {
    scaleNodes(1, 1 / 1.4);
  });
}

/**
 * Zoom at a specific screen position
 */
function zoomAt(screenX, screenY, factor) {
  const camera = AppState.getCamera();
  const wx = screenX / camera.scale - camera.x;
  const wy = screenY / camera.scale - camera.y;

  const newScale = Math.min(Math.max(camera.scale * factor, MIN_SCALE), MAX_SCALE);
  const newX = screenX / newScale - wx;
  const newY = screenY / newScale - wy;

  AppState.setCamera(newX, newY, newScale);
  doRender();
}

/**
 * Scale node positions around their center
 */
function scaleNodes(factorX, factorY) {
  const positions = AppState.getNodePositions();
  const positionValues = Object.values(positions);
  
  if (positionValues.length === 0) return;
  
  // Find center
  let cx = 0, cy = 0;
  for (const p of positionValues) {
    cx += p.x;
    cy += p.y;
  }
  cx /= positionValues.length;
  cy /= positionValues.length;
  
  // Scale positions around center
  for (const [id, p] of Object.entries(positions)) {
    const newX = cx + (p.x - cx) * factorX;
    const newY = cy + (p.y - cy) * factorY;
    AppState.setNodePosition(parseInt(id), newX, newY);
  }

  doRender();
}

/**
 * Fit all nodes into the visible canvas
 */
function fitToView() {
  const positions = AppState.getNodePositions();
  const positionValues = Object.values(positions);

  if (positionValues.length === 0) {
    AppState.resetCamera();
    doRender();
    return;
  }
  
  const container = document.getElementById('canvasContainer');
  const rect = container.getBoundingClientRect();
  const padding = 22 + 20; // NODE_RADIUS + padding
  
  // Find bounds
  let minX = Infinity, maxX = -Infinity;
  let minY = Infinity, maxY = -Infinity;
  
  for (const p of positionValues) {
    if (p.x < minX) minX = p.x;
    if (p.x > maxX) maxX = p.x;
    if (p.y < minY) minY = p.y;
    if (p.y > maxY) maxY = p.y;
  }
  
  const treeW = (maxX - minX) + padding * 2;
  const treeH = (maxY - minY) + padding * 2;
  
  const scaleX = rect.width / treeW;
  const scaleY = rect.height / treeH;
  const newScale = Math.min(scaleX, scaleY, 1.5);
  
  const centerX = (minX + maxX) / 2;
  const centerY = (minY + maxY) / 2;
  
  const newCamX = (rect.width / 2) / newScale - centerX;
  const newCamY = (rect.height / 2) / newScale - centerY;

  AppState.setCamera(newCamX, newCamY, newScale);
  doRender();
}

/**
 * Reset camera to default view
 */
function reset() {
  AppState.resetCamera();
  doRender();
}

export {
  init,
  zoomAt,
  scaleNodes,
  fitToView,
  reset
};

export default {
  init,
  zoomAt,
  scaleNodes,
  fitToView,
  reset
};
