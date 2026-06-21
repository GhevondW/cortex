#include <gtest/gtest.h>

#include "cortex/exec/executor.hpp"

namespace {

class InlineExecutor : public cortex::exec::Executor {
protected:
    InlineExecutor() = default;

public:
    static std::shared_ptr<InlineExecutor> Make() {
        return cortex::exec::Executor::Make<InlineExecutor>();
    }

    void Post(Task task) override {
        task();
    }
};

} // namespace

TEST(CortexExecExecutorApiTest, BasicTest) {
    auto executor = InlineExecutor::Make();
    int data = 0;
    executor->Post([&data, &executor] {
        ++data;
        auto self = executor->SelfAs<InlineExecutor>();
        self->Post([&data] {
            ++data;
        });
    });

    executor->Post([&data] {
        ++data;
    });

    EXPECT_EQ(data, 3);
}
