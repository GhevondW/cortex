// End-to-end tests for cortex::web::Await: C++ fibers awaiting JavaScript
// promises, driven by js/cortex.mjs.
// Usage: node await_test.mjs <module.mjs> <cortex.mjs>
import assert from "node:assert/strict";
import { test } from "node:test";
import { pathToFileURL } from "node:url";

const [modulePath, driverPath] = process.argv.slice(2);
const { default: createModule } = await import(pathToFileURL(modulePath).href);
const { drive } = await import(pathToFileURL(driverPath).href);

async function load() {
    const Module = await createModule();
    Module.makeDelayed = (value, ms) => new Promise((resolve) => setTimeout(() => resolve(value), ms));
    Module.makeRejected = (reason) => Promise.reject(new Error(reason));
    Module.makeUnprintableRejection = () => Promise.reject(Object.create(null));
    return Module;
}

test("a fiber awaits a promise's value", async () => {
    const Module = await load();
    const scheduler = Module._start_await_value();
    await drive(Module, scheduler).done;
    assert.equal(Module._int_result(), 42);
    Module._destroy_scheduler(scheduler);
});

test("a rejected promise throws JsError with the reason", async () => {
    const Module = await load();
    const scheduler = Module._start_await_rejection();
    await drive(Module, scheduler).done;
    assert.match(Module.UTF8ToString(Module._text_result()), /nope/);
    Module._destroy_scheduler(scheduler);
});

test("a rejection reason String() cannot convert still wakes the fiber", { timeout: 5000 }, async () => {
    const Module = await load();
    const scheduler = Module._start_await_unprintable_rejection();
    await drive(Module, scheduler).done;
    assert.match(Module.UTF8ToString(Module._text_result()), /JavaScript promise rejected: \[object Object\]/);
    Module._destroy_scheduler(scheduler);
});

test("fibers resume in the order their promises settle", async () => {
    const Module = await load();
    const scheduler = Module._start_await_two();
    await drive(Module, scheduler).done;
    assert.deepEqual([Module._order_at(0), Module._order_at(1)], [2, 1]);
    Module._destroy_scheduler(scheduler);
});

test("a promise settling after its scheduler is destroyed is harmless", async () => {
    const Module = await load();
    const scheduler = Module._start_await_forever();
    const driver = drive(Module, scheduler);
    await new Promise((resolve) => setTimeout(resolve, 5));
    driver.stop();
    await driver.done;
    Module._destroy_scheduler(scheduler);
    await new Promise((resolve) => setTimeout(resolve, 50)); // the promise settles now
});
