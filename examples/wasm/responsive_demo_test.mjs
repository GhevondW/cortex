// Smoke test for the Responsive C++ demo: loads its WebAssembly module in Node
// and drives it like the page does, so CI notices when the demo breaks.
// Usage: node responsive_demo_test.mjs <responsive_demo.js> <cortex.mjs>
import assert from "node:assert/strict";
import { test } from "node:test";
import { pathToFileURL } from "node:url";
import { resolve } from "node:path";

const [modulePath, driverPath] = process.argv.slice(2);
const { default: createModule } = await import(pathToFileURL(resolve(modulePath)).href);
const { drive } = await import(pathToFileURL(resolve(driverPath)).href);

const sleep = (ms) => new Promise((done) => setTimeout(done, ms));

test("renders in fibers, cancels a stale render, and reports a deadlock", async () => {
    const Module = await createModule();
    const logs = [];
    const cancelled = [];
    const finished = [];
    Module.onLog = (text) => logs.push(text);
    Module.onRenderCancelled = (id, rows) => cancelled.push({ id, rows });
    Module.onRenderDone = (id, ms) => finished.push({ id, ms });

    const H = Module._demo_height();
    const driver = drive(Module, Module._demo_start(), { budgetMs: 4 });

    // Render #1 is heavy; once it is under way, render #2 replaces it.
    Module._demo_render(-0.6, 0.0, 3.2, 4000);
    while (Module._demo_rows_done(0) === 0) {
        await sleep(1);
    }
    Module._demo_render(-0.6, 0.0, 3.2, 200);
    while (finished.length === 0) {
        await sleep(5);
    }
    assert.deepEqual(finished.map((f) => f.id), [2]);
    assert.equal(cancelled.length, 1);
    assert.equal(cancelled[0].id, 1);
    assert.ok(cancelled[0].rows > 0 && cancelled[0].rows < H, `render #1 stopped part-way (${cancelled[0].rows} rows)`);
    assert.equal(Module._demo_rows_done(0), H);

    // Node has no page to fetch palette.json from: the Await() rejection is
    // handled and the built-in palette is used.
    assert.ok(logs.some((line) => /built-in palette/.test(line)), logs.join("\n"));

    // The same renderer, called directly.
    assert.ok(Module._demo_render_plain(-0.6, 0.0, 3.2, 200) > 0);
    assert.equal(Module._demo_rows_done(1), H);

    // The fibers are visible while the controller waits for commands.
    assert.match(Module.UTF8ToString(Module._demo_describe_fibers()), /"controller" suspended in Channel::Receive/);

    await assert.rejects(drive(Module, Module._demo_start_deadlock()).done, (error) => {
        assert.match(error.message, /deadlock/);
        assert.match(error.message, /"client" suspended in Channel::Receive/);
        assert.match(error.message, /"server" suspended in Channel::Receive/);
        return true;
    });

    driver.stop();
    await driver.done;
});
