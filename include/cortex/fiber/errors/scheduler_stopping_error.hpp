#pragma once

#include <cortex/fiber/detail/platform.hpp>

#include <stdexcept>

namespace cortex::fiber {

/**
 * @class SchedulerStoppingError
 * @brief Thrown when a fiber operation is attempted while the scheduler is stopping.
 */
class SchedulerStoppingError : public std::runtime_error {
public:
    SchedulerStoppingError()
        : std::runtime_error("Scheduler is stopping") {}
};

} // namespace cortex::fiber
