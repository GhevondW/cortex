#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

#include <cortex/exec/executor.hpp>

namespace cortex::exec {

class SingleThreadExecutor : public Executor {
protected:
    explicit SingleThreadExecutor(std::size_t capacity);

public:
    ~SingleThreadExecutor() override;

    template <typename... Args>
    static std::shared_ptr<SingleThreadExecutor> Make(Args&&... args) {
        return cortex::exec::Executor::Make<SingleThreadExecutor>(std::forward<Args>(args)...);
    }

    bool Post(Task task) override;

private:
    void Worker();
    void Stop();

private:
    const std::size_t capacity_;
    std::queue<Task> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool closed_;

    std::thread worker_;
};

} // namespace cortex::exec
