#include <atomic>
#include <gtest/gtest.h>
#include <thread>

#include "cortex/exec/single_thread_executor.hpp"

TEST(CortexExecSingleThreadExecutorTest, BasicTest) {
    std::atomic<int> data = 0;

    {
        auto executor = cortex::exec::SingleThreadExecutor::Make(1024u);

        executor->Post([&data](auto& self) {
            data.fetch_add(1, std::memory_order_relaxed);

            self.Post([&data](auto&) {
                data.fetch_add(1, std::memory_order_relaxed);
            });
        });

        executor->Post([&data](auto&) {
            data.fetch_add(1, std::memory_order_relaxed);
        });

        // 2. Synchronize BEFORE destruction.
        // We must wait for the tasks to actually finish before `executor`
        // goes out of scope. If it goes out of scope too early, `closed_`
        // becomes true, and the nested Post() will be rejected.
        while (data.load(std::memory_order_acquire) < 3) {
            std::this_thread::yield();
        }
    } // destructor safely cleans up now

    EXPECT_EQ(data.load(std::memory_order_relaxed), 3);
}
