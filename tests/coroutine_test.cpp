#include "cortex/coroutine.hpp"
#include "cortex/errors/resume_on_completed_coroutine_error.hpp"
#include <gtest/gtest.h>
#include <vector>

TEST(CortexCoroutineTest, BasicExecution) {
    bool executed = false;
    auto cr = cortex::Coroutine::Make([&executed](cortex::CoroutineSuspendContext&) {
        executed = true;
    });

    EXPECT_FALSE(cr.IsDone());
    cr.Resume();
    EXPECT_TRUE(executed);
    EXPECT_TRUE(cr.IsDone());
}

TEST(CortexCoroutineTest, Suspension) {
    std::vector<int> values;
    auto cr = cortex::Coroutine::Make([&values](cortex::CoroutineSuspendContext& ctx) {
        values.push_back(1);
        ctx.Suspend();
        values.push_back(3);
    });

    EXPECT_FALSE(cr.IsDone());
    cr.Resume();
    values.push_back(2);
    EXPECT_FALSE(cr.IsDone());
    cr.Resume();
    EXPECT_TRUE(cr.IsDone());

    std::vector<int> expected = {1, 2, 3};
    EXPECT_EQ(values, expected);
}

TEST(CortexCoroutineTest, MultipleSuspensions) {
    int counter = 0;
    auto cr = cortex::Coroutine::Make([&counter](cortex::CoroutineSuspendContext& ctx) {
        for (int i = 0; i < 3; ++i) {
            counter++;
            ctx.Suspend();
        }
    });

    for (int i = 0; i < 3; ++i) {
        EXPECT_FALSE(cr.IsDone());
        cr.Resume();
        EXPECT_EQ(counter, i + 1);
    }
    EXPECT_FALSE(cr.IsDone());
    cr.Resume();
    EXPECT_TRUE(cr.IsDone());
    EXPECT_EQ(counter, 3);
}

TEST(CortexCoroutineTest, ExceptionHandling) {
    auto cr = cortex::Coroutine::Make([](cortex::CoroutineSuspendContext&) {
        throw std::runtime_error("Test exception");
    });

    EXPECT_FALSE(cr.IsDone());
    EXPECT_THROW(cr.Resume(), std::runtime_error);
    EXPECT_TRUE(cr.IsDone());
    EXPECT_TRUE(cr.HasException());
}

TEST(CortexCoroutineTest, ResumeOnDone) {
    auto cr = cortex::Coroutine::Make([](cortex::CoroutineSuspendContext&) {
    });
    cr.Resume();
    EXPECT_TRUE(cr.IsDone());
    EXPECT_THROW(cr.Resume(), cortex::ResumeOnDoneCoroutineError);
}
