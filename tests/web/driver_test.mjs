// End-to-end tests for js/cortex.mjs: a real WebAssembly module driven from
// Node's event loop. Usage: node driver_test.mjs <module.mjs> <cortex.mjs>
import assert from "node:assert/strict";
import { test } from "node:test";
import { pathToFileURL } from "node:url";

const [modulePath, driverPath] = process.argv.slice(2);
const { default: createModule } = await import(pathToFileURL(modulePath).href);
const { drive, Status } = await import(pathToFileURL(driverPath).href);

// Counts event-loop turns while `promise` is pending: proof the loop was free.
async function countTicksWhile(promise) {
    let ticks = 0;
    const interval = setInterval(() => ticks++, 1);
    try {
        await promise;
    } finally {
        clearInterval(interval);
    }
    return ticks;
}

test("Status mirrors Scheduler::Status", () => {
    assert.deepEqual(Status, { Runnable: 0, Waiting: 1, Done: 2, Deadlocked: 3, Failed: 4 });
});

test("sleeping fibers finish without blocking the event loop", async () => {
    const Module = await createModule();
    const scheduler = Module._start_sleeps();
    const start = performance.now();
    const ticks = await countTicksWhile(drive(Module, scheduler).done);
    assert.ok(performance.now() - start >= 58, "three 20 ms sleeps");
    assert.ok(ticks >= 10, `event loop kept running (ticks=${ticks})`);
    Module._destroy_scheduler(scheduler);
});

test("a busy loop with CheckPoint() yields to the event loop", async () => {
    const Module = await createModule();
    const scheduler = Module._start_busy_checkpoint();
    const ticks = await countTicksWhile(drive(Module, scheduler, { budgetMs: 4 }).done);
    assert.ok(ticks >= 10, `event loop ran during the computation (ticks=${ticks})`);
    Module._destroy_scheduler(scheduler);
});

test("a deadlock rejects with the stuck fibers", async () => {
    const Module = await createModule();
    const scheduler = Module._start_deadlock();
    await assert.rejects(drive(Module, scheduler).done, (error) => {
        assert.match(error.message, /deadlock/);
        assert.match(error.message, /"stuck" suspended in ConditionVariable::Wait/);
        return true;
    });
    Module._destroy_scheduler(scheduler);
});

test("an exception escaping a fiber rejects with its message", async () => {
    const Module = await createModule();
    const scheduler = Module._start_throwing();
    await assert.rejects(drive(Module, scheduler).done, /boom from fiber/);
    Module._destroy_scheduler(scheduler);
});

test("fulfilling a promise from JavaScript wakes the driver", async () => {
    const Module = await createModule();
    const scheduler = Module._start_promise();
    const { done } = drive(Module, scheduler);
    setTimeout(() => Module._resolve_promise(42), 20);
    await done;
    assert.equal(Module._promise_result(), 42);
    Module._destroy_scheduler(scheduler);
});

test("stop() ends driving", async () => {
    const Module = await createModule();
    const scheduler = Module._start_sleeps();
    const driver = drive(Module, scheduler);
    driver.stop();
    await driver.done;
    Module._destroy_scheduler(scheduler);
});
