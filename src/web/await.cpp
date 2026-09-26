#include <cortex/tiny_fiber/promise.hpp>
#include <cortex/web/await.hpp>

#include <emscripten.h>

#include <exception>
#include <memory>
#include <string>
#include <utility>

namespace cortex::web {

namespace {

using emscripten::val;

// One pending Await: owned by the JavaScript callbacks until the promise
// settles. If the awaiting fiber, or its whole scheduler, is gone by then,
// fulfilling it is harmless.
using PendingAwait = tiny_fiber::Promise<val>;

EM_JS_DEPS(cortex_web_await, "$Emval");

// Attach settle callbacks to `handle`'s promise. Promise.resolve() also
// accepts non-promise values, like JavaScript's `await`.
// (JavaScript body: keep clang-format away from it.)
// clang-format off
EM_JS(void, cortex_web_then, (emscripten::EM_VAL handle, void* pending), {
    Promise.resolve(Emval.toValue(handle)).then(
        function (value) { Module["_cortex_web_settle"](pending, 1, Emval.toHandle(value)); },
        function (reason) { Module["_cortex_web_settle"](pending, 0, Emval.toHandle(reason)); });
});

// A printable description of a rejection reason: its message, or String()
// of it. Never throws, whatever the reason is (a JavaScript exception must not
// unwind C++ frames).
EM_JS(emscripten::EM_VAL, cortex_web_describe, (emscripten::EM_VAL handle), {
    var reason = Emval.toValue(handle);
    var text;
    try {
        if (typeof reason === "string") {
            text = reason;
        } else if (reason !== null && reason !== undefined && typeof reason.message === "string") {
            text = reason.message;
        } else {
            text = String(reason);
        }
    } catch (e) {
        try {
            text = Object.prototype.toString.call(reason);
        } catch (e2) {
            text = "(a value that cannot be printed)";
        }
    }
    return Emval.toHandle(text);
});
// clang-format on

std::string DescribeReason(const val& reason) {
    return "JavaScript promise rejected: " +
           val::take_ownership(cortex_web_describe(reason.as_handle())).as<std::string>();
}

} // namespace

JsError::JsError(val reason)
    : std::runtime_error(DescribeReason(reason))
    , reason_(std::move(reason)) {}

val Await(val promise) {
    auto pending = std::make_unique<PendingAwait>(); // on the current scheduler; throws outside fibers
    auto future = pending->GetFuture();
    cortex_web_then(promise.as_handle(), pending.release());
    return future.Get();
}

} // namespace cortex::web

extern "C" {

// Called by the callbacks attached in cortex_web_then.
EMSCRIPTEN_KEEPALIVE void cortex_web_settle(void* pending, int fulfilled, emscripten::EM_VAL handle) {
    std::unique_ptr<cortex::web::PendingAwait> owner(static_cast<cortex::web::PendingAwait*>(pending));
    auto value = emscripten::val::take_ownership(handle);
    if (fulfilled != 0) {
        owner->SetValue(std::move(value));
    } else {
        owner->SetException(std::make_exception_ptr(cortex::web::JsError(std::move(value))));
    }
}

} // extern "C"
