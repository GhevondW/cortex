// Scenarios for cortex::web::Await, exported to tests/web/await_test.mjs.

#include <cortex/tiny_fiber/tiny_fiber.hpp>
#include <cortex/web/await.hpp>

#include <emscripten.h>
#include <emscripten/val.h>

#include <memory>
#include <string>
#include <vector>

namespace tf = cortex::tiny_fiber;
using emscripten::val;

namespace {

std::vector<std::unique_ptr<tf::Scheduler>> g_schedulers;
int g_int_result = 0;
std::string g_text_result;
std::vector<int> g_order;

void* Keep(std::unique_ptr<tf::Scheduler> scheduler) {
    g_schedulers.push_back(std::move(scheduler));
    return g_schedulers.back().get();
}

// Module.makeDelayed(value, ms): a promise resolving to value after ms.
val Delayed(int value, int ms) {
    return val::module_property("makeDelayed")(value, ms);
}

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void* start_await_value() {
    return Keep(tf::Scheduler::Create([] {
        g_int_result = cortex::web::Await(Delayed(42, 10)).as<int>();
    }));
}

EMSCRIPTEN_KEEPALIVE void* start_await_rejection() {
    return Keep(tf::Scheduler::Create([] {
        try {
            cortex::web::Await(val::module_property("makeRejected")(val("nope")));
            g_text_result = "no exception";
        } catch (const cortex::web::JsError& error) {
            g_text_result = error.what();
        }
    }));
}

// Module.makeUnprintableRejection(): rejected with a reason String() cannot
// convert (an object without a prototype).
EMSCRIPTEN_KEEPALIVE void* start_await_unprintable_rejection() {
    return Keep(tf::Scheduler::Create([] {
        try {
            cortex::web::Await(val::module_property("makeUnprintableRejection")());
            g_text_result = "no exception";
        } catch (const cortex::web::JsError& error) {
            g_text_result = error.what();
        }
    }));
}

// Two fibers await promises settling in the opposite order they started.
EMSCRIPTEN_KEEPALIVE void* start_await_two() {
    g_order.clear();
    return Keep(tf::Scheduler::Create([] {
        auto slow = tf::Spawn([] {
            g_order.push_back(cortex::web::Await(Delayed(1, 30)).as<int>());
        });
        auto fast = tf::Spawn([] {
            g_order.push_back(cortex::web::Await(Delayed(2, 5)).as<int>());
        });
    }));
}

// Awaits a promise that settles only after the scheduler was destroyed.
EMSCRIPTEN_KEEPALIVE void* start_await_forever() {
    return Keep(tf::Scheduler::Create([] {
        cortex::web::Await(Delayed(0, 30));
    }));
}

EMSCRIPTEN_KEEPALIVE int int_result() {
    return g_int_result;
}

EMSCRIPTEN_KEEPALIVE const char* text_result() {
    return g_text_result.c_str();
}

EMSCRIPTEN_KEEPALIVE int order_at(int index) {
    return g_order.at(static_cast<std::size_t>(index));
}

EMSCRIPTEN_KEEPALIVE void destroy_scheduler(void* scheduler) {
    std::erase_if(g_schedulers, [scheduler](const auto& owned) {
        return owned.get() == scheduler;
    });
}

} // extern "C"

int main() {
    return 0;
}
