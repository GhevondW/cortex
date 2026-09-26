// A module whose Emscripten runtime has exited cannot switch fibers any more;
// the driver must report why instead of a bogus deadlock.
// Usage: node exit_runtime_test.mjs <module.mjs> <cortex.mjs>
import assert from "node:assert/strict";
import { test } from "node:test";
import { pathToFileURL } from "node:url";

const [modulePath, driverPath] = process.argv.slice(2);
const { default: createModule } = await import(pathToFileURL(modulePath).href);
const { drive } = await import(pathToFileURL(driverPath).href);

test("driving fibers after the runtime exited rejects with the fix", async () => {
    const Module = await createModule(); // linked with -sEXIT_RUNTIME=1: exits after start-up
    const scheduler = Module._start_sleeps();
    await assert.rejects(drive(Module, scheduler).done, /EXIT_RUNTIME=0/);
});
