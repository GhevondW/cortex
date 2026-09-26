// Failures must never be silent: exceptions that nobody can observe, and
// fibers that can never finish, are reported to whoever drives the scheduler.

#include <cortex/tiny_fiber/tiny_fiber.hpp>

#include <gtest/gtest.h>

#include <exception>
#include <stdexcept>
#include <string>

namespace tf = cortex::tiny_fiber;

namespace {

void ParkForever() {
    tf::Mutex mutex;
    tf::ConditionVariable cv;
    auto guard = tf::Lock(mutex);
    cv.Wait(guard); // nobody will notify
}

} // namespace

TEST(TinyFiberErrors, RunRethrowsEntryException) {
    EXPECT_THROW(tf::Scheduler::Run([] {
                     throw std::runtime_error("disk full");
                 }),
                 std::runtime_error);
}

TEST(TinyFiberErrors, RunReturnsEntryValue) {
    EXPECT_EQ(tf::Scheduler::Run([] {
                  return 42;
              }),
              42);
}

TEST(TinyFiberErrors, RunReturnsMoveOnlyValue) {
    auto value = tf::Scheduler::Run([] {
        return std::make_unique<int>(7);
    });
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, 7);
}

TEST(TinyFiberErrors, RunReportsDeadlockWithDescription) {
    std::string message;
    try {
        tf::Scheduler::Run([] {
            tf::SetFiberName("stuck-main");
            ParkForever();
        });
    } catch (const tf::DeadlockError& e) {
        message = e.what();
    }
    EXPECT_NE(message.find("deadlock"), std::string::npos) << message;
    EXPECT_NE(message.find("stuck-main"), std::string::npos) << message;
    EXPECT_NE(message.find("ConditionVariable::Wait"), std::string::npos) << message;
}

TEST(TinyFiberErrors, RunReportsDeadlockOfDetachedFiber) {
    EXPECT_THROW(tf::Scheduler::Run([] {
                     tf::SpawnDetached([] {
                         ParkForever();
                     });
                 }),
                 tf::DeadlockError);
}

TEST(TinyFiberErrors, RunRethrowsDetachedFiberException) {
    EXPECT_THROW(tf::Scheduler::Run([] {
                     tf::SpawnDetached([] {
                         throw std::runtime_error("worker died");
                     });
                 }),
                 std::runtime_error);
}

TEST(TinyFiberErrors, StepRethrowsDetachedFiberException) {
    auto scheduler = tf::Scheduler::Create([] {
        tf::SpawnDetached([] {
            throw std::runtime_error("boom");
        });
    });
    EXPECT_THROW(
        {
            while (scheduler->Step()) {
            }
        },
        std::runtime_error);
}

TEST(TinyFiberErrors, StepRethrowsCreateEntryException) {
    auto scheduler = tf::Scheduler::Create([] {
        throw std::runtime_error("entry failed");
    });
    EXPECT_THROW(scheduler->Step(), std::runtime_error);
    EXPECT_TRUE(scheduler->IsDone());
}

TEST(TinyFiberErrors, UnhandledHandlerReceivesException) {
    std::string seen;
    tf::Scheduler::Config config;
    config.on_unhandled_exception = [&](std::exception_ptr ex) {
        try {
            std::rethrow_exception(ex);
        } catch (const std::exception& e) {
            seen = e.what();
        }
    };
    auto scheduler = tf::Scheduler::Create(
        [] {
            throw std::runtime_error("create-entry");
        },
        config);
    EXPECT_NO_THROW({
        while (scheduler->Step()) {
        }
    });
    EXPECT_EQ(seen, "create-entry");
}

TEST(TinyFiberErrors, DetachedFutureExceptionIsReported) {
    EXPECT_THROW(tf::Scheduler::Run([] {
                     auto future = tf::Spawn([] {
                         tf::Yield();
                         throw std::runtime_error("nobody is listening");
                     });
                     future.Detach();
                 }),
                 std::runtime_error);
}

TEST(TinyFiberErrors, StoppingIsNotReportedAsUnhandled) {
    auto scheduler = tf::Scheduler::Create([] {
        for (;;) {
            tf::Yield();
        }
    });
    scheduler->Step();
    scheduler->Stop();
    EXPECT_NO_THROW({
        while (!scheduler->IsDone()) {
            scheduler->Step();
        }
    });
}

TEST(TinyFiberErrors, StatusDistinguishesDoneAndDeadlocked) {
    auto stuck = tf::Scheduler::Create([] {
        ParkForever();
    });
    while (stuck->Step()) {
    }
    EXPECT_EQ(stuck->GetStatus(), tf::Scheduler::Status::kDeadlocked);
    EXPECT_FALSE(stuck->IsDone());
    EXPECT_EQ(stuck->GetFiberCount(), 1u);

    auto finished = tf::Scheduler::Create([] {
    });
    EXPECT_EQ(finished->GetStatus(), tf::Scheduler::Status::kRunnable);
    while (finished->Step()) {
    }
    EXPECT_EQ(finished->GetStatus(), tf::Scheduler::Status::kDone);
    EXPECT_TRUE(finished->IsDone());
    EXPECT_EQ(finished->GetFiberCount(), 0u);
}

TEST(TinyFiberErrors, DescribeFibersListsNamesStatesAndReasons) {
    auto scheduler = tf::Scheduler::Create([] {
        tf::SetFiberName("parent");
        auto child = tf::Spawn([] {
            tf::SetFiberName("worker");
            tf::Yield();
        });
        child.Wait();
    });
    scheduler->Step(); // parent spawns worker and parks in Future::Wait
    scheduler->Step(); // worker runs and yields: ready again
    const std::string text = scheduler->DescribeFibers();
    EXPECT_NE(text.find("2 live fiber"), std::string::npos) << text;
    EXPECT_NE(text.find("\"parent\""), std::string::npos) << text;
    EXPECT_NE(text.find("suspended in Future::Wait"), std::string::npos) << text;
    EXPECT_NE(text.find("\"worker\""), std::string::npos) << text;
    EXPECT_NE(text.find("ready"), std::string::npos) << text;
    while (scheduler->Step()) {
    }
}

TEST(TinyFiberErrors, SetFiberNameOutsideFiberThrows) {
    EXPECT_THROW(tf::SetFiberName("nope"), std::logic_error);
}
