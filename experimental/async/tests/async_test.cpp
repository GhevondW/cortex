#include <cortex/async/async.hpp>

#include <gtest/gtest.h>

namespace ca = cortex::async;

// ============================================================================
// Compilation tests — verify all types are complete and constructible
// ============================================================================

TEST(AsyncCompilation, ErrorTypes) {
    ca::RuntimeShutdownError e1;
    EXPECT_NE(std::string(e1.what()).find("shutting down"), std::string::npos);

    ca::TaskCancelledError e2;
    EXPECT_NE(std::string(e2.what()).find("cancelled"), std::string::npos);

    ca::ChannelClosedError e3;
    EXPECT_NE(std::string(e3.what()).find("closed"), std::string::npos);

    ca::NoExecutorError e4;
    EXPECT_NE(std::string(e4.what()).find("executor"), std::string::npos);
}

// ============================================================================
// Runtime stubs
// ============================================================================

TEST(AsyncRuntime, CreateThrows) {
    EXPECT_THROW(ca::Runtime::Create(), std::runtime_error);
}

TEST(AsyncRuntime, RunThrows) {
    EXPECT_THROW(ca::Runtime::Run([] {
                 }),
                 std::runtime_error);
}

TEST(AsyncRuntime, RunWithConfigThrows) {
    ca::Runtime::Config config;
    config.thread_count = 4;
    EXPECT_THROW(ca::Runtime::Run(
                     [] {
                     },
                     config),
                 std::runtime_error);
}

// ============================================================================
// Executor stubs
// ============================================================================

TEST(AsyncExecutor, SchedulerPolicyEnum) {
    auto ws = ca::SchedulerPolicy::kWorkStealing;
    auto rr = ca::SchedulerPolicy::kRoundRobin;
    auto pin = ca::SchedulerPolicy::kPinned;
    EXPECT_NE(ws, rr);
    EXPECT_NE(rr, pin);
}

// ============================================================================
// Future / Promise stubs
// ============================================================================

TEST(AsyncFuture, PromiseConstructible) {
    ca::Promise<int> promise;
    EXPECT_FALSE(promise.IsFulfilled());
}

TEST(AsyncFuture, PromiseSetValueThrows) {
    ca::Promise<int> promise;
    EXPECT_THROW(promise.SetValue(42), std::runtime_error);
}

TEST(AsyncFuture, PromiseGetFutureThrows) {
    ca::Promise<int> promise;
    EXPECT_THROW(promise.GetFuture(), std::runtime_error);
}

TEST(AsyncFuture, VoidPromiseConstructible) {
    ca::Promise<void> promise;
    EXPECT_FALSE(promise.IsFulfilled());
}

TEST(AsyncFuture, VoidPromiseSetValueThrows) {
    ca::Promise<void> promise;
    EXPECT_THROW(promise.SetValue(), std::runtime_error);
}

TEST(AsyncFuture, MakeReadyFutureThrows) {
    EXPECT_THROW(ca::MakeReadyFuture(42), std::runtime_error);
    EXPECT_THROW(ca::MakeReadyFuture(), std::runtime_error);
}

TEST(AsyncFuture, MakeExceptionalFutureThrows) {
    EXPECT_THROW(ca::MakeExceptionalFuture<int>(std::make_exception_ptr(std::runtime_error("test"))),
                 std::runtime_error);
}

// ============================================================================
// Sync primitive stubs
// ============================================================================

TEST(AsyncSync, MutexConstructible) {
    ca::sync::Mutex mutex;
    EXPECT_FALSE(mutex.IsLocked());
}

TEST(AsyncSync, MutexLockThrows) {
    ca::sync::Mutex mutex;
    EXPECT_THROW(mutex.Lock(), std::runtime_error);
}

TEST(AsyncSync, MutexTryLockThrows) {
    ca::sync::Mutex mutex;
    EXPECT_THROW(static_cast<void>(mutex.TryLock()), std::runtime_error);
}

TEST(AsyncSync, SharedMutexConstructible) {
    ca::sync::SharedMutex sm;
    EXPECT_THROW(sm.Lock(), std::runtime_error);
    EXPECT_THROW(sm.LockShared(), std::runtime_error);
}

TEST(AsyncSync, ConditionVariableConstructible) {
    ca::sync::ConditionVariable cv;
    EXPECT_THROW(cv.NotifyOne(), std::runtime_error);
    EXPECT_THROW(cv.NotifyAll(), std::runtime_error);
}

TEST(AsyncSync, BatonConstructible) {
    ca::sync::Baton baton;
    EXPECT_FALSE(baton.IsPosted());
    EXPECT_THROW(baton.Wait(), std::runtime_error);
    EXPECT_THROW(baton.Post(), std::runtime_error);
    EXPECT_THROW(baton.Reset(), std::runtime_error);
}

TEST(AsyncSync, SemaphoreConstructible) {
    ca::sync::Semaphore sem(5);
    EXPECT_EQ(sem.GetCount(), 0u); // stub returns 0
    EXPECT_THROW(sem.Acquire(), std::runtime_error);
    EXPECT_THROW(static_cast<void>(sem.TryAcquire()), std::runtime_error);
    EXPECT_THROW(sem.Release(), std::runtime_error);
}

TEST(AsyncSync, WaitGroupConstructible) {
    ca::sync::WaitGroup wg;
    EXPECT_EQ(wg.GetCount(), 0u);
    EXPECT_THROW(wg.Add(), std::runtime_error);
    EXPECT_THROW(wg.Done(), std::runtime_error);
    EXPECT_THROW(wg.Wait(), std::runtime_error);
}

TEST(AsyncSync, EventConstructible) {
    ca::sync::Event event_manual(ca::sync::EventResetPolicy::kManual);
    EXPECT_EQ(event_manual.GetPolicy(), ca::sync::EventResetPolicy::kManual);
    EXPECT_FALSE(event_manual.IsSignaled());

    ca::sync::Event event_auto(ca::sync::EventResetPolicy::kAutomatic);
    EXPECT_EQ(event_auto.GetPolicy(), ca::sync::EventResetPolicy::kAutomatic);

    EXPECT_THROW(event_manual.Wait(), std::runtime_error);
    EXPECT_THROW(event_manual.Signal(), std::runtime_error);
    EXPECT_THROW(event_manual.Reset(), std::runtime_error);
}

// ============================================================================
// Spawn / free function stubs
// ============================================================================

TEST(AsyncSpawn, YieldThrows) {
    EXPECT_THROW(ca::Yield(), std::runtime_error);
}

TEST(AsyncSpawn, YieldIfOthersReadyThrows) {
    EXPECT_THROW(ca::YieldIfOthersReady(), std::runtime_error);
}

TEST(AsyncSpawn, CurrentExecutorThrows) {
    EXPECT_THROW(ca::CurrentExecutor(), std::runtime_error);
}

TEST(AsyncSpawn, IsCancellationRequestedThrows) {
    EXPECT_THROW(ca::IsCancellationRequested(), std::runtime_error);
}

TEST(AsyncSpawn, IsShuttingDownThrows) {
    EXPECT_THROW(ca::IsShuttingDown(), std::runtime_error);
}

TEST(AsyncSpawn, SpawnThrows) {
    EXPECT_THROW(ca::Spawn([] {
                     return 42;
                 }),
                 std::runtime_error);
}

TEST(AsyncSpawn, SpawnVoidThrows) {
    EXPECT_THROW(ca::Spawn([] {
                 }),
                 std::runtime_error);
}

TEST(AsyncSpawn, SpawnOnThrows) {
    // Can't get an executor without a runtime, but we can verify the template compiles
    // by checking SpawnDetached which also requires an executor context
    EXPECT_THROW(ca::SpawnDetached([] {
                 }),
                 std::runtime_error);
}

TEST(AsyncSpawn, SleepForThrows) {
    EXPECT_THROW(ca::SleepFor(std::chrono::milliseconds(100)), std::runtime_error);
}

TEST(AsyncSpawn, SleepUntilThrows) {
    auto tp = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    EXPECT_THROW(ca::SleepUntil(tp), std::runtime_error);
}

// ============================================================================
// Channel compilation test
// ============================================================================

TEST(AsyncChannel, MakeChannelThrows) {
    EXPECT_THROW((ca::channel::MakeChannel<int>(10)), std::runtime_error);
}

TEST(AsyncChannel, MakeUnboundedChannelThrows) {
    EXPECT_THROW((ca::channel::MakeUnboundedChannel<int>()), std::runtime_error);
}
