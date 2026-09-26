// Minimal consumer used by the packaging checks: it must build with nothing but
// the cortex::cortex target and C++20.
#include <cortex/tiny_fiber/tiny_fiber.hpp>

#include <cstdio>

namespace tf = cortex::tiny_fiber;

int main() {
    int answer = 0;
    tf::Scheduler::Run([&answer] {
        auto a = tf::Spawn([] {
            tf::Yield();
            return 6;
        });
        answer = a.Get() * 7;
    });
    std::printf("%d\n", answer);
    return answer == 42 ? 0 : 1;
}
