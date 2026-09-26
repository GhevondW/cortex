#pragma once

#include <emscripten/val.h>

#include <stdexcept>

/**
 * @file await.hpp
 * @brief Await JavaScript promises from C++ fibers (WebAssembly only).
 *
 * Link the cortex::web target (it adds Emscripten's embind).
 */

namespace cortex::web {

/**
 * @class JsError
 * @brief A JavaScript promise that Await() waited on was rejected.
 */
class JsError : public std::runtime_error {
public:
    /// what() is "JavaScript promise rejected: " plus the reason's message
    /// (or String(reason)).
    explicit JsError(emscripten::val reason);

    /// The rejection reason, as the JavaScript value.
    [[nodiscard]] const emscripten::val& Reason() const noexcept {
        return reason_;
    }

private:
    emscripten::val reason_;
};

/**
 * @brief Suspend the calling fiber — not the page — until `promise` settles.
 *
 * Other fibers keep running, and the JavaScript event loop keeps rendering
 * and handling input, while the promise is pending. Drive the scheduler with
 * js/cortex.mjs (or anything that calls RunFor() and honours the scheduler's
 * wake-up handler), so the fiber resumes as soon as the promise settles.
 *
 * @code
 * using emscripten::val;
 * val response = cortex::web::Await(val::global("fetch")(url));
 * std::string text = cortex::web::Await(response.call<val>("text")).as<std::string>();
 * @endcode
 *
 * Non-promise values are awaited like JavaScript's `await` does: they
 * resolve to themselves.
 *
 * Only a rejection becomes a C++ exception. A JavaScript exception thrown
 * synchronously by a `val` call (JSON.parse on bad input, a DOM exception, a
 * failed `as<T>()` conversion) is not one: it unwinds the WebAssembly stack
 * without running C++ destructors and, inside a fiber, leaves the module
 * unable to run fibers (js/cortex.mjs then rejects every drive with that
 * error). Check values before converting them, or call code that may throw
 * through a promise (`Promise.resolve().then(...)`) and Await() it.
 *
 * @return The fulfilled value.
 * @throws JsError if the promise is rejected.
 * @throws std::logic_error if called outside of a fiber.
 * @throws CancelledError / SchedulerStoppingError while waiting.
 */
emscripten::val Await(emscripten::val promise);

} // namespace cortex::web
