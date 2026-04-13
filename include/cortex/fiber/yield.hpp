#pragma once

#include <cortex/fiber/detail/platform.hpp>

namespace cortex::fiber {

void Yield();
bool YieldIfOthersReady();
bool IsStopping();

} // namespace cortex::fiber
