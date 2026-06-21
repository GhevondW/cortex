#include "cortex/exec/single_thread_executor.hpp"

#include <iostream>
#include <utility> // for std::move, std::swap

namespace cortex::exec {

SingleThreadExecutor::SingleThreadExecutor(std::size_t capacity)
    : capacity_(capacity)
    , queue_()
    , mutex_()
    , cv_()
    , closed_(false)
    , worker_(&SingleThreadExecutor::Worker, this) {}

SingleThreadExecutor::~SingleThreadExecutor() {
    Stop();
}

bool SingleThreadExecutor::Post(Task task) {
    std::lock_guard lock(mutex_);

    if (closed_) {
        return false;
    }

    if (queue_.size() >= capacity_) {
        return false;
    }

    queue_.push(std::move(task));

    cv_.notify_one();
    return true;
}

void SingleThreadExecutor::Worker() {
    while (true) {
        std::queue<Task> tasks;
        {
            std::unique_lock lock(mutex_);

            cv_.wait(lock, [&] {
                return closed_ || !queue_.empty();
            });

            // Exit only when closed AND the queue is fully drained
            if (closed_ && queue_.empty()) {
                return;
            }

            std::swap(queue_, tasks);
        }

        while (!tasks.empty()) {
            auto task = std::move(tasks.front());
            tasks.pop();

            try {
                task(*this);
            } catch (...) {
                std::cerr << "task error\n";
            }
        }
    }
}

void SingleThreadExecutor::Stop() {
    {
        std::lock_guard lock(mutex_);
        if (closed_) {
            return;
        }
        closed_ = true;
    }
    cv_.notify_one();

    if (worker_.joinable()) {
        worker_.join();
    }
}

} // namespace cortex::exec