#pragma once

#include <cortex/exec/executor.hpp>

namespace cortex::exec {

class StrandExecutor : public Executor {
protected:
    StrandExecutor() = default;

public:
    ~StrandExecutor() override;

    static std::shared_ptr<StrandExecutor> Make() {
        return cortex::exec::Executor::Make<StrandExecutor>();
    }

    void Post(Task task) override {
        task();
    }

private:

};

}
