// Cortex micro-benchmarks.
//
// Measures the library's hot paths: raw coroutine context switches,
// coroutine creation (stack allocation), fiber spawn/join, cooperative
// yields and generator throughput.
//
// Usage: cortex_bench [--csv] [filter-substring]
//
// --csv prints "name,ns_per_op" lines for scripted comparison (see
// benchmarks/compare.py and benchmarks/ci_bench_check.sh).

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <cortex/coroutine.hpp>
#include <cortex/generator.hpp>
#include <cortex/pooled_memory_resource.hpp>
#include <cortex/tiny_fiber/tiny_fiber.hpp>

namespace {

namespace tf = cortex::tiny_fiber;

using Clock = std::chrono::steady_clock;

std::string g_filter;
bool g_csv = false;

template <typename Op>
double TimeNsPerOp(std::uint64_t iterations, Op&& op) {
    const auto start = Clock::now();
    for (std::uint64_t i = 0; i < iterations; ++i) {
        op();
    }
    const auto stop = Clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
    return static_cast<double>(elapsed) / static_cast<double>(iterations);
}

// Runs `make_ns()` kRepeats times and reports the best (least noisy) run.
template <typename MakeNs>
void RunBench(const char* name, MakeNs&& make_ns) {
    if (!g_filter.empty() && std::string(name).find(g_filter) == std::string::npos) {
        return;
    }

    constexpr int kRepeats = 3;
    double best_ns = 0.0;
    for (int i = 0; i < kRepeats; ++i) {
        const double ns = make_ns();
        if (i == 0 || ns < best_ns) {
            best_ns = ns;
        }
    }

    if (g_csv) {
        std::printf("%s,%.2f\n", name, best_ns);
        return;
    }

    const double ops_per_sec = best_ns > 0.0 ? 1e9 / best_ns : 0.0;
    std::printf("%-42s %12.1f ns/op %15.0f ops/s\n", name, best_ns, ops_per_sec);
}

// One Resume() of a coroutine parked in Suspend(): enter + exit switch pair.
void BenchCoroutineSwitch() {
    RunBench("coroutine_switch (resume+suspend)", [] {
        auto coroutine = cortex::Coroutine::Make([](cortex::CoroutineSuspendContext& ctx) {
            for (;;) {
                ctx.Suspend();
            }
        });

        constexpr std::uint64_t kIterations = 2'000'000;
        return TimeNsPerOp(kIterations, [&] {
            coroutine.Resume();
        });
    });
}

// Create a coroutine, run it to completion, destroy it. Dominated by the
// stack allocation in the memory resource.
void BenchCoroutineCreateDestroy(const char* name, const cortex::MemoryResourceSharedPtr& resource) {
    RunBench(name, [&] {
        constexpr std::uint64_t kIterations = 50'000;
        return TimeNsPerOp(kIterations, [&] {
            auto coroutine = cortex::Coroutine::Make(
                [](cortex::CoroutineSuspendContext&) {
                },
                cortex::Coroutine::kDefaultStackSizeBytes,
                resource);
            coroutine.Resume();
        });
    });
}

// Spawn a fiber returning int and Get() its result: covers fiber + stack
// allocation, ready-queue round trip, waiter wakeup and future state.
void BenchFiberSpawnJoin(const char* name, tf::Scheduler::Config config) {
    RunBench(name, [&config] {
        double ns = 0.0;
        tf::Scheduler::Run(
            [&ns] {
                constexpr std::uint64_t kIterations = 20'000;
                ns = TimeNsPerOp(kIterations, [] {
                    auto future = tf::Spawn([] {
                        return 42;
                    });
                    (void)future.Get();
                });
            },
            config);
        return ns;
    });
}

// N fibers yielding in a loop: scheduler queue + context switch cost.
void BenchFiberYield() {
    RunBench("fiber_yield (8 fibers round-robin)", [] {
        constexpr std::uint64_t kFibers = 8;
        constexpr std::uint64_t kYieldsPerFiber = 100'000;

        double ns = 0.0;
        tf::Scheduler::Run([&ns] {
            std::vector<tf::Future<void>> futures;
            futures.reserve(kFibers);

            const auto start = Clock::now();
            for (std::uint64_t i = 0; i < kFibers; ++i) {
                futures.push_back(tf::Spawn([] {
                    for (std::uint64_t j = 0; j < kYieldsPerFiber; ++j) {
                        tf::Yield();
                    }
                }));
            }
            for (auto& future : futures) {
                future.Wait();
            }
            const auto stop = Clock::now();

            const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
            ns = static_cast<double>(elapsed) / static_cast<double>(kFibers * kYieldsPerFiber);
        });
        return ns;
    });
}

// Generator Next()/DetachValue() round trip.
void BenchGenerator() {
    RunBench("generator_next (yield int)", [] {
        auto generator = cortex::Generator<int>::Make([](cortex::Generator<int>::YieldContext& yield) {
            for (int i = 0;; ++i) {
                yield(i);
            }
        });

        constexpr std::uint64_t kIterations = 1'000'000;
        return TimeNsPerOp(kIterations, [&] {
            generator.Next();
            (void)generator.DetachValue();
        });
    });
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--csv") {
            g_csv = true;
        } else {
            g_filter = arg;
        }
    }

    if (!g_csv) {
        std::printf("cortex benchmarks (best of 3 runs)\n");
        std::printf("----------------------------------------------------------------------------\n");
    }

    BenchCoroutineSwitch();
    BenchCoroutineCreateDestroy("coroutine_create_destroy (default alloc)", cortex::GetDefaultMemoryResource());
    BenchCoroutineCreateDestroy("coroutine_create_destroy (pooled alloc)", cortex::MakePooledMemoryResource());
    BenchFiberSpawnJoin("fiber_spawn_join (default config)", tf::Scheduler::Config {});
    BenchFiberSpawnJoin("fiber_spawn_join (unpooled alloc)",
                        tf::Scheduler::Config {.memory_resource = cortex::GetDefaultMemoryResource()});
    BenchFiberYield();
    BenchGenerator();

    return 0;
}
