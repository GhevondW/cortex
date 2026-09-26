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
 * @return The fulfilled value.
 * @throws JsError if the promise is rejected.
 * @throws std::logic_error if called outside of a fiber.
 * @throws CancelledError / SchedulerStoppingError while waiting.
 */
emscripten::val Await(emscripten::val promise);

} // namespace cortex::web
