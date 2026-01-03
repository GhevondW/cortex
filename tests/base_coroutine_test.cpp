#include <cortex/base_coroutine.hpp>
#include <gtest/gtest.h>
#include <vector>

namespace cortex {
namespace {

class SimpleCoroutine : public BaseCoroutine {
public:
    using BaseCoroutine::BaseCoroutine;

    bool executed = false;

private:
    void Continuation(CoroutineSuspendContext&) override {
        executed = true;
    }
};

TEST(BaseCoroutineTest, BasicExecution) {
    SimpleCoroutine cr;
    EXPECT_FALSE(cr.IsDone());
    EXPECT_FALSE(cr.executed);

    cr.Resume();

    EXPECT_TRUE(cr.executed);
    EXPECT_TRUE(cr.IsDone());
}

class SuspendingCoroutine : public BaseCoroutine {
public:
    using BaseCoroutine::BaseCoroutine;

    std::vector<int> values;

private:
    void Continuation(CoroutineSuspendContext& self) override {
        values.push_back(1);
        self.Suspend();
        values.push_back(3);
    }
};

TEST(BaseCoroutineTest, Suspension) {
    SuspendingCoroutine cr;

    EXPECT_FALSE(cr.IsDone());

    cr.Resume();
    cr.values.push_back(2);

    EXPECT_FALSE(cr.IsDone());

    cr.Resume();

    EXPECT_TRUE(cr.IsDone());

    std::vector<int> expected = {1, 2, 3};
    EXPECT_EQ(cr.values, expected);
}

class ExceptionCoroutine : public BaseCoroutine {
public:
    using BaseCoroutine::BaseCoroutine;

private:
    void Continuation(CoroutineSuspendContext&) override {
        throw std::runtime_error("Test exception");
    }
};

TEST(BaseCoroutineTest, ExceptionHandling) {
    ExceptionCoroutine cr;

    EXPECT_FALSE(cr.IsDone());
    EXPECT_THROW(cr.Resume(), std::runtime_error);
    EXPECT_TRUE(cr.IsDone());
}

} // namespace
} // namespace cortex
