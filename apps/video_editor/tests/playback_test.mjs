// Tests for web/src/playback.js's cooperative path, with a fake video and a
// fake engine: how many display ticks a frame needs decides the frame rate.
// Usage: node --test playback_test.mjs
import assert from "node:assert/strict";
import { test } from "node:test";

globalThis.requestAnimationFrame ??= () => 0;
globalThis.cancelAnimationFrame ??= () => {};

const { Playback } = await import("../web/src/playback.js");

// Let queued macrotasks (the cooperative pump) run.
async function drainMacrotasks(turns = 100) {
    for (let i = 0; i < turns; ++i) {
        await new Promise((resolve) => setImmediate(resolve));
    }
}

// A playing video: a display tick fires when the test delivers a new frame.
class FakeVideo {
    playing = true;
    hasVideoFrameCallback = true;
    position = 0;
    frames = 0;
    #pending = null;

    requestVideoFrame(callback) {
        this.#pending = callback;
        return 1;
    }
    cancelVideoFrame() {
        this.#pending = null;
    }
    produce() {
        return 0; // the frame is written into the engine's slot 0
    }
    deliverFrame() {
        const callback = this.#pending;
        this.#pending = null;
        this.frames += 1;
        callback?.();
    }
}

// Stand-in for the engine: each frame needs `workMs` of filtering, and
// runCooperativeFor(budget) does up to `budget` of it.
class FakeEngine {
    begun = 0;
    painted = 0;
    #remaining = 0;

    constructor(workMs) {
        this.workMs = workMs;
    }
    beginCooperativeRender() {
        this.begun += 1;
        this.#remaining = this.workMs;
    }
    runCooperativeFor(budgetMs) {
        this.#remaining -= budgetMs;
    }
    cooperativeDone() {
        return this.#remaining <= 0;
    }
    renderPreview() {}
    outputFrame() {
        return new Uint8ClampedArray(4);
    }
    width() {
        return 1;
    }
    height() {
        return 1;
    }
}

function startCooperativePlayback(engine) {
    const playback = new Playback({ client: engine, canvas: { draw: () => (engine.painted += 1) } });
    playback.setCooperative(true);
    const video = new FakeVideo();
    playback.setProvider(video);
    playback.start();
    return video;
}

test("a frame needing several slices is painted before the next video frame", async () => {
    const engine = new FakeEngine(30); // e.g. a heavy blur: 30 ms of filtering
    const video = startCooperativePlayback(engine);

    video.deliverFrame();
    await drainMacrotasks();

    // Finished between video frames, as a synchronous render would, instead of
    // one slice per video frame (which divided the frame rate by the number of
    // slices).
    assert.equal(engine.painted, 1);
});

test("no work is repeated while no new frame arrives", async () => {
    const engine = new FakeEngine(30);
    const video = startCooperativePlayback(engine);

    video.deliverFrame();
    await drainMacrotasks();
    await drainMacrotasks();

    assert.equal(engine.begun, 1);
    assert.equal(engine.painted, 1);
});

test("frames arriving during a render are rendered next, once", async () => {
    const engine = new FakeEngine(30);
    const video = startCooperativePlayback(engine);

    video.deliverFrame(); // starts a render
    video.deliverFrame(); // two more frames arrive before it finishes...
    video.deliverFrame();
    await drainMacrotasks();

    // ...and collapse into one follow-up render of the latest frame.
    assert.equal(engine.begun, 2);
    assert.equal(engine.painted, 2);
});
