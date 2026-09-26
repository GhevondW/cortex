/**
 * @file responsive_demo.cpp
 * @brief Responsive C++: a Mandelbrot explorer whose renderer runs in
 *        cortex::tiny_fiber fibers, so the page stays interactive while
 *        heavy C++ computes.
 *
 * What it shows (responsive_demo.html is the page, js/cortex.mjs drives it):
 * - RenderRows() is plain C++ with tf::CheckPoint() once per row. Called
 *   directly ("plain call" mode) it freezes the page until it returns; run in
 *   fibers it hands the thread back every few milliseconds.
 * - A render splits the image into bands, one fiber each (Spawn + WaitAll):
 *   they share the thread and progress together.
 * - Page events arrive through a Channel fed from JavaScript; a new view
 *   cancels the render in flight (Future::Cancel()), which stops its band
 *   fibers at their next CheckPoint().
 * - The palette is fetched with cortex::web::Await(fetch(...)); the tour
 *   sleeps between waypoints with tf::SleepFor().
 * - "Cause a deadlock" runs a buggy client/server on a second scheduler: the
 *   driver rejects with a report naming the stuck fibers.
 */

#include <cortex/tiny_fiber/tiny_fiber.hpp>
#include <cortex/web/await.hpp>

#include <emscripten.h>
#include <emscripten/val.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace tf = cortex::tiny_fiber;
using emscripten::val;
using namespace std::chrono_literals;

namespace {

constexpr int kWidth = 640;
constexpr int kHeight = 400;
constexpr int kBands = 4;

struct View {
    double cx;
    double cy;
    double scale; // width of the view in the complex plane
    int max_iter;
};

struct Color {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
};

// Hooks into the page (responsive_demo.html sets them on Module).
EM_JS_DEPS(responsive_demo, "$UTF8ToString");
// clang-format off
EM_JS(void, js_render_done, (int id, double ms), {
    if (Module["onRenderDone"]) Module["onRenderDone"](id, ms);
});
EM_JS(void, js_render_cancelled, (int id, int rows), {
    if (Module["onRenderCancelled"]) Module["onRenderCancelled"](id, rows);
});
EM_JS(void, js_view, (double cx, double cy, double scale, int max_iter), {
    if (Module["onView"]) Module["onView"](cx, cy, scale, max_iter);
});
EM_JS(void, js_log, (const char* text), {
    if (Module["onLog"]) Module["onLog"](UTF8ToString(text));
});
// clang-format on

void Log(const std::string& text) {
    js_log(text.c_str());
}

// --- Rendering: ordinary C++ ------------------------------------------------

std::array<Color, 256> MakePalette(std::span<const Color> stops) {
    std::array<Color, 256> palette {};
    const auto segments = static_cast<double>(stops.size() - 1);
    for (std::size_t i = 0; i < palette.size(); ++i) {
        const double position = static_cast<double>(i) / 255.0 * segments;
        const auto at = std::min(static_cast<std::size_t>(position), stops.size() - 2);
        const double t = position - static_cast<double>(at);
        const auto mix = [t](std::uint8_t a, std::uint8_t b) {
            return static_cast<std::uint8_t>(std::lround(a + (b - a) * t));
        };
        palette[i] = {
            mix(stops[at].r, stops[at + 1].r), mix(stops[at].g, stops[at + 1].g), mix(stops[at].b, stops[at + 1].b)};
    }
    return palette;
}

constexpr std::array<Color, 5> kBuiltInStops {{
    {0, 7, 100},
    {32, 107, 203},
    {237, 255, 255},
    {255, 170, 0},
    {0, 2, 0},
}};

std::array<Color, 256> g_palette = MakePalette(kBuiltInStops);

std::vector<std::uint8_t> g_fiber_pixels(static_cast<std::size_t>(kWidth) * kHeight * 4);
std::vector<std::uint8_t> g_plain_pixels(static_cast<std::size_t>(kWidth) * kHeight * 4);
int g_fiber_rows_done = 0;
int g_plain_rows_done = 0;

// Renders rows [y0, y1) of `view` into `pixels` (RGBA). Nothing here knows
// about fibers: CheckPoint() yields when this fiber's time slice is spent,
// and does nothing at all when the function is called directly.
void RenderRows(const View& view, int y0, int y1, std::uint8_t* pixels, int& rows_done) {
    const double step = view.scale / kWidth;
    const double left = view.cx - view.scale / 2.0;
    const double top = view.cy - step * kHeight / 2.0;
    for (int y = y0; y < y1; ++y) {
        const double ci = top + y * step;
        std::uint8_t* row = pixels + static_cast<std::size_t>(y) * kWidth * 4;
        for (int x = 0; x < kWidth; ++x) {
            const double cr = left + x * step;
            double zr = 0.0;
            double zi = 0.0;
            int n = 0;
            while (n < view.max_iter && zr * zr + zi * zi <= 16.0) {
                const double next = zr * zr - zi * zi + cr;
                zi = 2.0 * zr * zi + ci;
                zr = next;
                ++n;
            }
            Color color {0, 0, 0};
            if (n < view.max_iter) {
                // Smooth colouring: a fractional escape count avoids bands.
                const double smooth = n + 1.0 - std::log2(std::log2(zr * zr + zi * zi) / 2.0);
                color = g_palette[static_cast<std::size_t>(std::max(0.0, smooth * 6.0)) % g_palette.size()];
            }
            row[x * 4 + 0] = color.r;
            row[x * 4 + 1] = color.g;
            row[x * 4 + 2] = color.b;
            row[x * 4 + 3] = 255;
        }
        ++rows_done;
        tf::CheckPoint(); // one check per row is plenty
    }
}

// --- Fibers --------------------------------------------------------------------

enum class CommandKind {
    kRender,
    kStartTour,
    kStopTour
};

struct Command {
    CommandKind kind;
    View view;
};

std::unique_ptr<tf::Scheduler> g_scheduler;
std::unique_ptr<tf::Channel<Command>> g_commands; // fed from JavaScript
std::unique_ptr<tf::Scheduler> g_deadlock_scheduler;

// Fetches palette.json with the browser's fetch(). Only this fiber waits for
// the network; the page and the other fibers keep running.
void LoadPalette() {
    try {
        val response = cortex::web::Await(val::global("fetch")(std::string("palette.json")));
        if (!response["ok"].as<bool>()) {
            throw std::runtime_error("HTTP " + std::to_string(response["status"].as<int>()));
        }
        const val stops = cortex::web::Await(response.call<val>("json"));
        // Check the shape before converting: a JavaScript exception thrown by
        // a failed conversion must not unwind C++ frames.
        const val array = val::global("Array");
        if (!stops.instanceof (array)) {
            throw std::runtime_error("palette.json is not an array");
        }
        std::vector<Color> colors;
        for (int i = 0; i < stops["length"].as<int>(); ++i) {
            const val stop = stops[i];
            if (!stop.instanceof (array) || stop["length"].as<int>() != 3 || !stop[0].isNumber() ||
                                     !stop[1].isNumber() || !stop[2].isNumber()) {
                throw std::runtime_error("palette.json entries must be [r, g, b]");
            }
            const auto channel = [&stop](int c) {
                return static_cast<std::uint8_t>(std::clamp(stop[c].as<int>(), 0, 255));
            };
            colors.push_back({channel(0), channel(1), channel(2)});
        }
        if (colors.size() < 2) {
            throw std::runtime_error("palette.json needs at least two colours");
        }
        g_palette = MakePalette(colors);
        Log("palette.json arrived via fetch(), awaited by the controller fiber");
    } catch (const std::exception& error) {
        Log(std::string("using the built-in palette (") + error.what() + ")");
    }
}

// Renders `view` with one fiber per band, and reports how it went.
void RenderView(int id, View view) {
    tf::SetFiberName("render #" + std::to_string(id));
    const auto start = std::chrono::steady_clock::now();
    g_fiber_rows_done = 0;

    std::vector<tf::Future<void>> bands;
    for (int band = 0; band < kBands; ++band) {
        bands.push_back(tf::Spawn([view, band] {
            tf::SetFiberName("band " + std::to_string(band + 1));
            RenderRows(
                view, kHeight * band / kBands, kHeight * (band + 1) / kBands, g_fiber_pixels.data(), g_fiber_rows_done);
        }));
    }

    bool cancelled = false;
    try {
        tf::WaitAll(std::span(bands));
    } catch (const tf::CancelledError&) {
        cancelled = true; // a newer view arrived: Future::Cancel() on this fiber
    }
    const int rows = g_fiber_rows_done;
    bands.clear(); // joins the bands; a cancelled render cancels them too

    if (cancelled) {
        js_render_cancelled(id, rows);
        return;
    }
    const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
    js_render_done(id, elapsed.count());
}

// Zooms into Seahorse Valley and back, sleeping between waypoints.
void Tour() {
    tf::SetFiberName("tour");
    constexpr std::array<View, 6> kWaypoints {{
        {-0.6, 0.0, 3.2, 300},
        {-0.745, 0.12, 0.35, 500},
        {-0.7436, 0.1318, 0.05, 800},
        {-0.743644, 0.131826, 0.006, 1200},
        {-0.7436439, 0.1318259, 0.0008, 1600},
        {-0.74364389, 0.13182590, 0.00012, 2200},
    }};
    for (;;) {
        for (const View& waypoint : kWaypoints) {
            js_view(waypoint.cx, waypoint.cy, waypoint.scale, waypoint.max_iter);
            g_commands->Send(Command {CommandKind::kRender, waypoint});
            tf::SleepFor(2500ms); // Future::Cancel() interrupts the sleep
        }
    }
}

// The controller: turns commands from the page into fibers.
void Controller() {
    tf::SetFiberName("controller");
    LoadPalette();

    std::optional<tf::Future<void>> render;
    std::optional<tf::Future<void>> tour;
    int next_id = 1;
    for (const Command& command : *g_commands) { // waits for the page
        switch (command.kind) {
        case CommandKind::kRender:
            if (render) {
                render->Cancel(); // stops at the bands' next CheckPoint()
                render.reset(); // and waits for that, so buffers are free
            }
            render.emplace(tf::Spawn([id = next_id++, view = command.view] {
                RenderView(id, view);
            }));
            break;
        case CommandKind::kStartTour:
            if (!tour || tour->IsReady()) {
                tour.emplace(tf::Spawn(Tour));
                Log("tour started: a fiber that sleeps between waypoints");
            }
            break;
        case CommandKind::kStopTour:
            if (tour) {
                tour->Cancel();
                tour.reset();
                Log("tour stopped: Future::Cancel() woke it from SleepFor()");
            }
            break;
        }
    }
}

} // namespace

extern "C" {

// Creates the scheduler for js/cortex.mjs's drive().
EMSCRIPTEN_KEEPALIVE void* demo_start() {
    g_scheduler = tf::Scheduler::Create(Controller);
    // Created with the scheduler, from plain code: fed from outside, so while
    // the controller waits on it the scheduler reports "waiting", not
    // "deadlocked".
    g_commands = std::make_unique<tf::Channel<Command>>(*g_scheduler);
    return g_scheduler.get();
}

EMSCRIPTEN_KEEPALIVE int demo_width() {
    return kWidth;
}

EMSCRIPTEN_KEEPALIVE int demo_height() {
    return kHeight;
}

EMSCRIPTEN_KEEPALIVE std::uint8_t* demo_pixels(int plain) {
    return plain != 0 ? g_plain_pixels.data() : g_fiber_pixels.data();
}

EMSCRIPTEN_KEEPALIVE int demo_rows_done(int plain) {
    return plain != 0 ? g_plain_rows_done : g_fiber_rows_done;
}

// Asks the fibers for a new view. TrySend() never suspends, so it is safe to
// call from a JavaScript event handler; it wakes the controller and the
// driver.
EMSCRIPTEN_KEEPALIVE void demo_render(double cx, double cy, double scale, int max_iter) {
    g_commands->TrySend(Command {CommandKind::kRender, View {cx, cy, scale, max_iter}});
}

EMSCRIPTEN_KEEPALIVE void demo_tour(int start) {
    g_commands->TrySend(Command {start != 0 ? CommandKind::kStartTour : CommandKind::kStopTour, View {}});
}

// The same RenderRows(), called directly: no fiber, so CheckPoint() does
// nothing and the page is frozen until it returns. Returns milliseconds.
EMSCRIPTEN_KEEPALIVE double demo_render_plain(double cx, double cy, double scale, int max_iter) {
    const auto start = std::chrono::steady_clock::now();
    g_plain_rows_done = 0;
    RenderRows(View {cx, cy, scale, max_iter}, 0, kHeight, g_plain_pixels.data(), g_plain_rows_done);
    const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
    return elapsed.count();
}

// Every live fiber and what it waits in (Scheduler::DescribeFibers()).
EMSCRIPTEN_KEEPALIVE const char* demo_describe_fibers() {
    static std::string text;
    text = g_scheduler ? g_scheduler->DescribeFibers() : std::string();
    return text.c_str();
}

// A buggy client/server on a scheduler of its own: the client waits for a
// reply before sending its request. Driving it rejects with a deadlock
// report that names both fibers.
EMSCRIPTEN_KEEPALIVE void* demo_start_deadlock() {
    g_deadlock_scheduler = tf::Scheduler::Create([] {
        tf::SetFiberName("client");
        tf::Channel<int> requests;
        tf::Channel<int> replies;
        auto server = tf::Spawn([&requests, &replies] {
            tf::SetFiberName("server");
            for (int request : requests) {
                replies.Send(request * 2);
            }
        });
        const int reply = replies.Receive().value_or(0); // bug: nothing was sent yet
        requests.Send(reply);
    });
    return g_deadlock_scheduler.get();
}

} // extern "C"
