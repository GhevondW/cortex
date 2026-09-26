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

// setTimeout() treats longer delays as 1 ms.
const MAX_TIMEOUT_MS = 2 ** 31 - 1;

// Module -> { drivers: Map(scheduler pointer -> driver hooks), broken: error | null }.
const modules = new WeakMap();

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

function stateOf(Module) {
    let state = modules.get(Module);
    if (!state) {
        state = { drivers: new Map(), broken: null };
        modules.set(Module, state);
        // Called by the C++ side: a scheduler has runnable work again, or is
        // being destroyed.
        Module.cortexWake = (scheduler) => state.drivers.get(scheduler)?.wake();
        Module.cortexForget = (scheduler) => state.drivers.get(scheduler)?.forget();
    }
    return state;
}

// The error drivers report once the module cannot run fibers any more.
function unusableModuleError(cause) {
    const reason = cause instanceof Error ? cause.message : String(cause);
    return new Error(
        `cortex: this WebAssembly module can no longer run fibers: a trap or a JavaScript ` +
            `exception interrupted one (${reason})`,
        { cause },
    );
}

/**
 * Drive `scheduler` (a Scheduler* returned by your module) from the event loop.
 *
 * - Drive a scheduler once at a time; calling drive() again for a scheduler
 *   that is still being driven throws.
 * - `done` resolves when every fiber finished, when `stop()` is called, or when
 *   the scheduler is destroyed from C++. It rejects with an Error on a deadlock
 *   (the message lists the stuck fibers), when an exception escapes a fiber
 *   nobody waits for (its what()), and when a trap or a JavaScript exception
 *   interrupts a fiber. The last one leaves the module unable to run fibers
 *   (C++ destructors did not run, the fiber runtime is mid-switch), so every
 *   other drive on the module rejects too, and later drive() calls reject
 *   straight away.
 * - `stop()` only stops driving: it resolves `done` and leaves the fibers where
 *   they are. To cancel the work itself, call an export of yours that calls
 *   Scheduler::Stop() or Future::Cancel(), or destroy the scheduler.
 *
 * The module must keep its runtime alive (-sEXIT_RUNTIME=0, the default except
 * with -fsanitize=address).
 *
 * @param {object} Module The instantiated Emscripten module.
 * @param {number} scheduler Pointer to a cortex::tiny_fiber::Scheduler.
 * @param {{budgetMs?: number}} [options] budgetMs: longest a slice of fiber
 *        work may run before the browser gets the thread back (default 8).
 * @returns {{done: Promise<void>, stop: () => void}}
 */
export function drive(Module, scheduler, { budgetMs = 8 } = {}) {
    if (typeof Module._cortex_scheduler_run_for !== "function") {
        throw new Error("cortex: this WebAssembly module does not link the cortex scheduler");
    }
    const state = stateOf(Module);
    if (state.drivers.has(scheduler)) {
        throw new Error("cortex: this scheduler is already being driven");
    }
    if (state.broken !== null) {
        return { done: Promise.reject(unusableModuleError(state.broken)), stop() {} };
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

    // Stop driving. `detach` unless the scheduler is gone or the module broke.
    const finish = (detach) => {
        finished = true;
        if (timer !== null) {
            clearTimeout(timer);
            timer = null;
        }
        macrotasks.close();
        state.drivers.delete(scheduler);
        if (detach) {
            Module._cortex_scheduler_detach(scheduler);
        }
    };

    // A trap or a JavaScript exception unwound the WebAssembly stack in the
    // middle of a fiber: fail every drive on this module.
    const breakModule = (error) => {
        state.broken = error;
        for (const other of [...state.drivers.values()]) {
            other.fail(other.tick === tick ? error : unusableModuleError(error));
        }
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
            // The return value of an export that switched fibers is a placeholder under
            // Asyncify; read the recorded outcome instead.
            Module._cortex_scheduler_run_for(scheduler, budgetMs);
            status = Module._cortex_scheduler_last_status();
        } catch (error) {
            breakModule(error);
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
                    timer = setTimeout(tick, Math.min(ms, MAX_TIMEOUT_MS));
                }
                break;
            }
            case Status.Done:
                finish(true);
                resolveDone();
                break;
            case Status.Deadlocked:
                finish(true);
                rejectDone(new Error(Module.cortexMessage ?? "cortex: deadlock"));
                break;
            default:
                finish(true);
                rejectDone(new Error(Module.cortexMessage ?? "cortex: a fiber failed"));
                break;
        }
    };

    state.drivers.set(scheduler, {
        tick,
        wake: schedule,
        // The scheduler is being destroyed: its memory may be reused.
        forget() {
            finish(false);
            resolveDone();
        },
        fail(error) {
            finish(false);
            rejectDone(error);
        },
    });
    Module._cortex_scheduler_attach(scheduler);
    schedule();

    return {
        done,
        stop() {
            if (!finished) {
                finish(true);
                resolveDone();
            }
        },
    };
}
