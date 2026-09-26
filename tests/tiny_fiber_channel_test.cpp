// Channel<T>: FIFO message passing between fibers, bounded or unbounded, and
// feedable from plain code (e.g. an event callback between scheduler steps).

#include <cortex/tiny_fiber/tiny_fiber.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace tf = cortex::tiny_fiber;
using namespace std::chrono_literals;

TEST(TinyFiberChannel, ProducerConsumerPreservesOrder) {
    std::vector<int> received;
    tf::Scheduler::Run([&] {
        tf::Channel<int> channel;
        auto producer = tf::Spawn([&] {
            for (int i = 0; i < 5; ++i) {
                EXPECT_TRUE(channel.Send(i));
                tf::Yield();
            }
            channel.Close();
        });
        while (auto value = channel.Receive()) {
            received.push_back(*value);
        }
    });
    EXPECT_EQ(received, (std::vector<int> {0, 1, 2, 3, 4}));
}

TEST(TinyFiberChannel, BoundedCapacityBlocksSender) {
    int max_in_flight = 0;
    tf::Scheduler::Run([&] {
        tf::Channel<int> channel(2);
        int sent = 0;
        int received = 0;
        auto producer = tf::Spawn([&] {
            for (int i = 0; i < 10; ++i) {
                channel.Send(i);
                ++sent;
                max_in_flight = std::max(max_in_flight, sent - received);
            }
            channel.Close();
        });
        for (int value : channel) {
            (void)value;
            ++received;
            tf::Yield(); // slow consumer
        }
        EXPECT_EQ(received, 10);
    });
    EXPECT_LE(max_in_flight, 3); // capacity 2, plus the one just handed over
}

TEST(TinyFiberChannel, CloseDrainsBufferedValuesThenEndsRangeFor) {
    std::vector<std::string> received;
    tf::Scheduler::Run([&] {
        tf::Channel<std::string> channel;
        channel.Send("a");
        channel.Send("b");
        channel.Close();
        EXPECT_TRUE(channel.IsClosed());
        for (auto& value : channel) {
            received.push_back(value);
        }
    });
    EXPECT_EQ(received, (std::vector<std::string> {"a", "b"}));
}

TEST(TinyFiberChannel, SendAfterCloseReturnsFalse) {
    tf::Scheduler::Run([] {
        tf::Channel<int> channel;
        channel.Close();
        EXPECT_FALSE(channel.Send(1));
        EXPECT_FALSE(channel.TrySend(1));
        EXPECT_EQ(channel.Receive(), std::nullopt);
    });
}

TEST(TinyFiberChannel, CloseWakesBlockedReceiversAndSenders) {
    tf::Scheduler::Run([] {
        tf::Channel<int> full(1);
        full.Send(0);
        tf::Channel<int> empty;
        auto sender = tf::Spawn([&] {
            return full.Send(1);
        });
        auto receiver = tf::Spawn([&] {
            return empty.Receive();
        });
        tf::Yield();
        full.Close();
        empty.Close();
        EXPECT_FALSE(sender.Get());
        EXPECT_EQ(receiver.Get(), std::nullopt);
    });
}

TEST(TinyFiberChannel, TrySendAndTryReceiveNeverBlock) {
    tf::Scheduler::Run([] {
        tf::Channel<int> channel(1);
        EXPECT_EQ(channel.TryReceive(), std::nullopt);
        EXPECT_TRUE(channel.TrySend(7));
        EXPECT_FALSE(channel.TrySend(8)); // full
        EXPECT_EQ(channel.Size(), 1u);
        EXPECT_EQ(channel.TryReceive(), 7);
    });
}

TEST(TinyFiberChannel, CancelBlockedReceiver) {
    tf::Scheduler::Run([] {
        tf::Channel<int> channel;
        auto receiver = tf::Spawn([&] {
            return channel.Receive();
        });
        tf::Yield();
        receiver.Cancel();
        EXPECT_THROW((void)receiver.Get(), tf::CancelledError);
    });
}

TEST(TinyFiberChannel, ZeroCapacityIsRejected) {
    tf::Scheduler::Run([] {
        EXPECT_THROW(tf::Channel<int>(0), std::invalid_argument);
    });
}

TEST(TinyFiberChannel, FedFromPlainCodeBetweenSteps) {
    std::unique_ptr<tf::Channel<int>> channel;
    std::vector<int> received;
    auto scheduler = tf::Scheduler::Create([&] {
        for (int value : *channel) {
            received.push_back(value);
        }
    });
    channel = std::make_unique<tf::Channel<int>>(*scheduler);

    scheduler->RunFor(1ms);
    // Bound to the scheduler from plain code: fed from outside, so waiting on
    // it is waiting on the outside world, not a deadlock.
    EXPECT_EQ(scheduler->GetStatus(), tf::Scheduler::Status::kWaiting);
    EXPECT_TRUE(channel->TrySend(1)); // e.g. from an input-event callback
    EXPECT_TRUE(channel->TrySend(2));
    scheduler->RunFor(1ms);
    channel->Close();
    EXPECT_EQ(scheduler->RunFor(1ms), tf::Scheduler::Status::kDone);
    EXPECT_EQ(received, (std::vector<int> {1, 2}));
}

// Every kind of wait must end on Stop(): each waiter either throws
// SchedulerStoppingError or (WaitAny) returns because a future it watches
// finished with that error. Nothing may stay parked, and no timer may remain.
TEST(TinyFiberChannel, StopWakesEveryKindOfWait) {
    int exited = 0;
    auto scheduler = tf::Scheduler::Create([&] {
        auto count_stop = [&exited](auto&& body) {
            return [&exited, body]() mutable {
                try {
                    body();
                } catch (const tf::SchedulerStoppingError&) {
                }
                ++exited;
            };
        };
        auto channel = std::make_shared<tf::Channel<int>>();
        auto sleeper = std::make_shared<tf::Future<void>>(tf::Spawn([] {
            tf::SleepFor(1h);
        }));
        tf::SpawnDetached(count_stop([] {
            tf::SleepFor(1h);
        }));
        tf::SpawnDetached(count_stop([channel] {
            (void)channel->Receive();
        }));
        tf::SpawnDetached(count_stop([sleeper] {
            sleeper->Wait();
        }));
        tf::SpawnDetached(count_stop([sleeper] {
            auto other = tf::Spawn([] {
                tf::SleepFor(1h);
            });
            (void)tf::WaitAny(*sleeper, other);
        }));
        tf::SpawnDetached(count_stop([] {
            tf::Mutex mutex;
            tf::ConditionVariable cv;
            auto guard = tf::Lock(mutex);
            (void)cv.WaitFor(guard, 1h);
        }));
        auto promise = std::make_shared<tf::Promise<int>>();
        tf::SpawnDetached(count_stop([promise] {
            (void)promise->GetFuture().Get();
        }));
        // `sleeper` stays shared with the fibers above; the entry just returns.
    });
    scheduler->RunFor(5ms);
    ASSERT_EQ(scheduler->GetStatus(), tf::Scheduler::Status::kWaiting);
    scheduler->Stop();
    while (!scheduler->IsDone()) {
        scheduler->Step();
    }
    EXPECT_EQ(exited, 6);
    EXPECT_FALSE(scheduler->NextTimerDeadline().has_value());
}

// A receiver woken for a value but cancelled before it runs must hand the
// wake-up on: the value must not sit in the buffer next to a parked receiver.
TEST(TinyFiberChannel, CancelledWokenReceiverPassesTheValueOn) {
    tf::Scheduler::Run([] {
        tf::Channel<int> channel;
        auto first = tf::Spawn([&] {
            return channel.Receive();
        });
        auto second = tf::Spawn([&] {
            return channel.Receive();
        });
        tf::Yield(); // both park, first in line
        channel.Send(1); // wakes `first`
        first.Cancel(); // ...which is cancelled before it runs
        EXPECT_THROW((void)first.Get(), tf::CancelledError);
        EXPECT_EQ(second.Get(), 1);
    });
}

TEST(TinyFiberChannel, CancelledWokenSenderPassesTheSpaceOn) {
    tf::Scheduler::Run([] {
        tf::Channel<int> channel(1);
        channel.Send(0); // full
        auto first = tf::Spawn([&] {
            return channel.Send(1);
        });
        auto second = tf::Spawn([&] {
            return channel.Send(2);
        });
        tf::Yield(); // both park, first in line
        EXPECT_EQ(channel.TryReceive(), 0); // frees a slot: wakes `first`
        first.Cancel();
        EXPECT_THROW((void)first.Get(), tf::CancelledError);
        EXPECT_TRUE(second.Get());
        EXPECT_EQ(channel.TryReceive(), 2);
    });
}
