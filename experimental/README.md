# Experimental

Code here is **not** part of the cortex library: it is not built by default,
not installed, and its API may change or disappear.

| Module | Status |
|---|---|
| `async/` — `cortex::async` multithreaded runtime (work-stealing executors, `Task<T>`, `Promise<T>`, channels, sync primitives) | API design only. Every function throws `"Not implemented yet"`. |

Build it with `-DCORTEX_BUILD_EXPERIMENTAL=ON`.

The single-threaded features this design sketched (futures, promises,
channels, timers, cancellation) are available today in
`cortex::tiny_fiber`.
