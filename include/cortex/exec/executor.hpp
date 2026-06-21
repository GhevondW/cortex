#pragma once

#include <memory>

#include <function2/function2.hpp>

namespace cortex::exec {

class Executor;
using ExecutorPtr = std::shared_ptr<Executor>;

class Executor : public std::enable_shared_from_this<Executor> {
protected:
    Executor() = default;

public:
    using Task = fu2::unique_function<void(Executor&)>;

    template <typename T, typename... Args>
    static std::shared_ptr<T> Make(Args&&... args) {
        struct EnableMakeShared : T {
            explicit EnableMakeShared(Args&&... args)
                : T(std::forward<Args>(args)...) {}
        };

        return std::make_shared<EnableMakeShared>(std::forward<Args>(args)...);
    }

    Executor(Executor&&) noexcept = delete;
    Executor& operator=(Executor&&) noexcept = delete;
    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;
    virtual ~Executor() = default;

    ExecutorPtr Self() {
        return shared_from_this();
    }

    template <typename T>
    ExecutorPtr SelfAs() {
        return std::static_pointer_cast<T>(Self());
    }

    virtual bool Post(Task task) = 0;
};

} // namespace cortex::exec
