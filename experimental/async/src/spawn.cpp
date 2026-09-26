#include <cortex/async/spawn.hpp>

#include <stdexcept>

namespace cortex::async {

void Yield() {
    throw std::runtime_error("Not implemented yet");
}

bool YieldIfOthersReady() {
    throw std::runtime_error("Not implemented yet");
}

Executor& CurrentExecutor() {
    throw std::runtime_error("Not implemented yet");
}

bool IsCancellationRequested() {
    throw std::runtime_error("Not implemented yet");
}

bool IsShuttingDown() {
    throw std::runtime_error("Not implemented yet");
}

} // namespace cortex::async
