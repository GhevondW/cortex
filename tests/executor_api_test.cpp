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

    bool Post(Task task) override {
        task(*this);
        return true;
    }
};

} // namespace

TEST(CortexExecExecutorApiTest, BasicTest) {
    auto executor = InlineExecutor::Make();
    int data = 0;
    executor->Post([&data](auto& self) {
        ++data;
        self.Post([&data](auto&) {
            ++data;
        });
    });

    executor->Post([&data](auto&) {
        ++data;
    });

    EXPECT_EQ(data, 3);
}
