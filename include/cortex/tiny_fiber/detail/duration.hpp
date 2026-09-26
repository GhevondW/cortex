#pragma once

#include <chrono>

namespace cortex::tiny_fiber::detail {

// `duration` in steady_clock ticks, rounded up and clamped to the range of
// steady_clock::duration. Converting a huge value directly (hours::max() is a
// common way to say "forever") overflows and wraps around to a negative wait.
template <typename Rep, typename Period>
constexpr std::chrono::steady_clock::duration SaturatingCeil(std::chrono::duration<Rep, Period> duration) noexcept {
    using Target = std::chrono::steady_clock::duration;
    using Wide = std::chrono::duration<long double, Target::period>;
    if (Wide(duration) >= Wide(Target::max())) {
        return Target::max();
    }
    if (Wide(duration) <= Wide(Target::min())) {
        return Target::min();
    }
    return std::chrono::ceil<Target>(duration);
}

} // namespace cortex::tiny_fiber::detail
