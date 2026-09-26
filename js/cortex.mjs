// Cortex browser driver.
//
// Runs a cortex::tiny_fiber::Scheduler from the JavaScript event loop in
// time-boxed slices, so C++ fibers make progress while the page keeps
// rendering and handling input:
//
//   * while fibers are runnable, it runs them for `budgetMs` per macrotask
//     (a MessageChannel message in browsers, setImmediate in Node), so input,
//     timers and rendering interleave with the work;
//   * while they sleep, it waits with setTimeout until the next timer;
//   * while they wait on the outside world (a Promise fulfilled from JS, a
//     Channel fed from JS), it sleeps until the scheduler wakes it;
//   * it settles `done` when every fiber finished, and rejects it on a
//     deadlock or an exception escaping a fiber.
//
// Usage:
//   import { drive } from "./cortex.mjs";
//   const Module = await createModule();          // your Emscripten module
//   const scheduler = Module._start_my_work();     // returns a Scheduler*
//   const { done, stop } = drive(Module, scheduler, { budgetMs: 8 });
//   await done;

/** Mirrors cortex::tiny_fiber::Scheduler::Status, plus Failed. */
export const Status = Object.freeze({ Runnable: 0, Waiting: 1, Done: 2, Deadlocked: 3, Failed: 4 });

// Module -> Map(scheduler pointer -> wake function).
const drivers = new WeakMap();

// Queue `callback` as a new macrotask. Node's MessagePort drains messages
// posted from a message handler before running timers, which would starve
// them; setImmediate runs after timers and I/O instead.
function makeMacrotaskQueue(callback) {
    if (typeof setImmediate === "function") {
        let handle = null;
        return {
            post() {
                handle = setImmediate(() => {
                    handle = null;
                    callback();
                });
            },
            close() {
                if (handle !== null) {
                    clearImmediate(handle);
                }
            },
        };
    }
    const channel = new MessageChannel();
    channel.port1.onmessage = callback;
    return {
        post() {
            channel.port2.postMessage(null);
        },
        close() {
            channel.port1.onmessage = null;
            channel.port1.close();
            channel.port2.close();
        },
    };
}

function driversOf(Module) {
    let map = drivers.get(Module);
    if (!map) {
        map = new Map();
        drivers.set(Module, map);
        Module.cortexWake = (scheduler) => map.get(scheduler)?.();
    }
    return map;
}

/**
 * Drive `scheduler` (a Scheduler* returned by your module) from the event loop.
 *
 * @param {object} Module The instantiated Emscripten module.
 * @param {number} scheduler Pointer to a cortex::tiny_fiber::Scheduler.
 * @param {{budgetMs?: number}} [options] budgetMs: CPU time per slice (default 8).
 * @returns {{done: Promise<void>, stop: () => void}}
 */
export function drive(Module, scheduler, { budgetMs = 8 } = {}) {
    if (typeof Module._cortex_scheduler_run_for !== "function") {
        throw new Error("cortex: this WebAssembly module does not link the cortex scheduler");
    }

    let resolveDone;
    let rejectDone;
    const done = new Promise((resolve, reject) => {
        resolveDone = resolve;
        rejectDone = reject;
    });

    let finished = false;
    let queued = false;
    let timer = null;
    const macrotasks = makeMacrotaskQueue(() => {
        queued = false;
        tick();
    });

    const schedule = () => {
        if (!finished && !queued) {
            queued = true;
            macrotasks.post();
        }
    };

    const finish = () => {
        finished = true;
        if (timer !== null) {
            clearTimeout(timer);
            timer = null;
        }
        macrotasks.close();
        driversOf(Module).delete(scheduler);
        Module._cortex_scheduler_detach(scheduler);
    };

    const tick = () => {
        if (finished) {
            return;
        }
        if (timer !== null) {
            clearTimeout(timer);
            timer = null;
        }

        let status;
        try {
            // The return value of an export that switched fibers is lost under
            // Asyncify; read the recorded outcome instead.
            Module._cortex_scheduler_run_for(scheduler, budgetMs);
            status = Module._cortex_scheduler_last_status();
        } catch (error) {
            finish();
            rejectDone(error);
            return;
        }

        switch (status) {
            case Status.Runnable:
                schedule();
                break;
            case Status.Waiting: {
                // Sleep until the next timer; external events wake us earlier
                // through Module.cortexWake.
                const ms = Module._cortex_scheduler_next_timer_ms(scheduler);
                if (ms >= 0) {
                    timer = setTimeout(tick, ms);
                }
                break;
            }
            case Status.Done:
                finish();
                resolveDone();
                break;
            case Status.Deadlocked:
                finish();
                rejectDone(new Error(Module.cortexMessage ?? "cortex: deadlock"));
                break;
            default:
                finish();
                rejectDone(new Error(Module.cortexMessage ?? "cortex: a fiber failed"));
                break;
        }
    };

    driversOf(Module).set(scheduler, schedule);
    Module._cortex_scheduler_attach(scheduler);
    schedule();

    return {
        done,
        stop() {
            if (!finished) {
                finish();
                resolveDone();
            }
        },
    };
}
