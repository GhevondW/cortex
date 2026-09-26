/**
 * @file runtime_tour.cpp
 * @brief A tour of the cortex::tiny_fiber runtime, one feature per section.
 *
 * Each section runs its own scheduler; the whole tour takes well under a
 * second. Build with -DCORTEX_BUILD_EXAMPLES=ON and run
 * ./build/examples/runtime_tour.
 */

#include <cortex/tiny_fiber/tiny_fiber.hpp>

#include <chrono>
#include <cstdio>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace tf = cortex::tiny_fiber;
using namespace std::chrono_literals;

namespace {

void Section(const char* title) {
    std::printf("\n== %s\n", title);
}

// Ordinary code: called directly it simply runs; inside a fiber, CheckPoint()
// yields whenever this fiber has used up its time slice (2 ms by default).
long CountPrimes(int limit) {
    long count = 0;
    for (int n = 2; n < limit; ++n) {
        bool prime = true;
        for (int d = 2; d * d <= n && prime; ++d) {
            prime = n % d != 0;
        }
        count += prime ? 1 : 0;
        tf::CheckPoint();
    }
    return count;
}

void TimeSlicing() {
    Section("1. Time slicing: a long computation shares the thread");

    // Outside a fiber CheckPoint() does nothing.
    std::printf("direct call: %ld primes below 100000\n", CountPrimes(100'000));

    const long primes = tf::Scheduler::Run([] {
        auto work = tf::Spawn([] {
            return CountPrimes(400'000);
        });

        // This fiber keeps running on schedule while the primes are counted.
        int ticks = 0;
        while (!work.IsReady()) {
            tf::SleepFor(5ms);
            ++ticks;
        }
        std::printf("a 5 ms ticker ran %d times during the computation\n", ticks);
        return work.Get(); // Run() returns what the entry returns
    });
    std::printf("in a fiber: %ld primes below 400000\n", primes);
}

void Cancellation() {
    Section("2. Timeouts and cancellation");

    tf::Scheduler::Run([] {
        auto download = tf::Spawn([] {
            tf::SetFiberName("download");
            tf::SleepFor(10s); // stands in for slow I/O
            return std::string("payload");
        });
        if (!download.WaitFor(50ms)) {
            std::puts("no answer within 50 ms: cancelling");
            download.Cancel(); // wakes the fiber: SleepFor throws CancelledError
        }
        try {
            std::printf("got %s\n", download.Get().c_str());
        } catch (const tf::CancelledError&) {
            std::puts("Get() threw CancelledError");
        }

        // Cancelling a fiber also cancels the fibers it spawned.
        int stopped_children = 0;
        auto parent = tf::Spawn([&stopped_children] {
            auto spin = [&stopped_children] {
                try {
                    for (;;) {
                        tf::CheckPoint(); // a cancellation point
                    }
                } catch (const tf::CancelledError&) {
                    ++stopped_children;
                    throw;
                }
            };
            auto first = tf::Spawn(spin);
            auto second = tf::Spawn(spin);
            tf::WaitAll(first, second);
        });
        tf::SleepFor(10ms);
        parent.Cancel();
        tf::WaitAll(parent); // unlike Wait(), does not rethrow its CancelledError
        std::printf("cancelling the parent stopped %d children\n", stopped_children);
    });
}

void Pipeline() {
    Section("3. Channels: a producer / consumer pipeline");

    const int lines_received = tf::Scheduler::Run([] {
        tf::Channel<int> numbers(/*capacity=*/4); // bounded: Send() waits while full
        tf::Channel<std::string> lines; // unbounded

        auto producer = tf::Spawn([&numbers] {
            for (int i = 1; i <= 10; ++i) {
                numbers.Send(i);
            }
            numbers.Close(); // ends the range-for loops downstream
        });
        auto formatter = tf::Spawn([&numbers, &lines] {
            for (int n : numbers) {
                lines.Send(std::to_string(n) + " squared is " + std::to_string(n * n));
            }
            lines.Close();
        });

        int count = 0;
        for (const std::string& line : lines) {
            if (count < 3) {
                std::printf("%s\n", line.c_str());
            }
            ++count;
        }
        return count;
    });
    std::printf("... %d lines in total\n", lines_received);
}

void FirstResponder() {
    Section("4. WaitAny: take the fastest answer, cancel the rest");

    tf::Scheduler::Run([] {
        const auto ask = [](const char* mirror, std::chrono::milliseconds latency) {
            return tf::Spawn([mirror, latency] {
                tf::SleepFor(latency);
                return std::string(mirror);
            });
        };
        std::vector<tf::Future<std::string>> mirrors;
        mirrors.push_back(ask("eu", 30ms));
        mirrors.push_back(ask("us", 10ms));
        mirrors.push_back(ask("asia", 50ms));

        const std::size_t first = tf::WaitAny(std::span(mirrors));
        std::printf("fastest mirror: %s\n", mirrors[first].Get().c_str());
        for (auto& mirror : mirrors) {
            mirror.Cancel(); // the others stop waiting right away
        }
    });
}

void BackgroundThread() {
    Section("5. Promise + Post(): a result from another thread");

    tf::Scheduler::Run([] {
        tf::Promise<long> promise;
        tf::Future<long> result = promise.GetFuture();

        // Post() is the thread-safe way in: the worker hands the value to the
        // scheduler's thread, which fulfils the promise.
        std::thread worker([&scheduler = tf::Scheduler::Current(), promise = std::move(promise)]() mutable {
            long sum = 0;
            for (long i = 0; i < 20'000'000; ++i) {
                sum += i % 7;
            }
            scheduler.Post([promise = std::move(promise), sum]() mutable {
                promise.SetValue(sum);
            });
        });

        auto heartbeat = tf::Spawn([&result] {
            int beats = 0;
            while (!result.IsReady()) {
                tf::SleepFor(2ms);
                ++beats;
            }
            return beats;
        });

        // Only this fiber waits; Run() sleeps until the posted work arrives.
        std::printf("worker thread computed %ld\n", result.Get());
        std::printf("the heartbeat fiber beat %d times meanwhile\n", heartbeat.Get());
        worker.join();
    });
}

void DeadlockReport() {
    Section("6. No silent hangs: a deadlock names the stuck fibers");

    try {
        tf::Scheduler::Run([] {
            tf::Channel<int> requests;
            tf::Channel<int> replies;
            auto server = tf::Spawn([&requests, &replies] {
                tf::SetFiberName("server");
                for (int request : requests) {
                    replies.Send(request * 2);
                }
            });
            tf::SetFiberName("client");
            // Bug: the client waits for a reply without sending a request.
            std::printf("reply: %d\n", replies.Receive().value_or(-1));
        });
    } catch (const tf::DeadlockError& error) {
        std::printf("Run() threw DeadlockError:\n%s\n", error.what());
    }
}

void OwnEventLoop() {
    Section("7. Driving a scheduler from your own event loop");

    // What js/cortex.mjs does in the browser: run fibers for a budget per
    // "frame", sleep while they sleep.
    auto scheduler = tf::Scheduler::Create([] {
        auto ticker = tf::Spawn([] {
            for (int i = 0; i < 3; ++i) {
                std::printf("tick %d\n", i);
                tf::SleepFor(20ms);
            }
        });
        auto primes = tf::Spawn([] {
            return CountPrimes(300'000);
        });
        std::printf("primes: %ld\n", primes.Get());
    });

    int frames = 0;
    for (bool running = true; running; ++frames) {
        switch (scheduler->RunFor(4ms)) {
        case tf::Scheduler::Status::kRunnable:
            break; // more work is ready: a real loop renders a frame here
        case tf::Scheduler::Status::kWaiting:
            // Nothing to run: sleep until the next timer is due.
            if (auto deadline = scheduler->NextTimerDeadline()) {
                std::this_thread::sleep_until(*deadline);
            }
            break;
        case tf::Scheduler::Status::kDone:
            running = false;
            break;
        case tf::Scheduler::Status::kDeadlocked:
            std::printf("deadlock:\n%s\n", scheduler->DescribeFibers().c_str());
            running = false;
            break;
        }
    }
    std::printf("finished in %d slices of at most 4 ms\n", frames);
}

} // namespace

int main() {
    TimeSlicing();
    Cancellation();
    Pipeline();
    FirstResponder();
    BackgroundThread();
    DeadlockReport();
    OwnEventLoop();
    return 0;
}
