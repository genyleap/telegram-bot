#include "telegrambot.hpp"
#include <cassert>
#include <chrono>
#include <thread>

int main() {
    RateLimiter limiter(2, std::chrono::seconds(1));

    assert(limiter.allowRequest("user-1"));
    assert(limiter.allowRequest("user-1"));
    assert(!limiter.allowRequest("user-1"));

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    assert(limiter.allowRequest("user-1"));

    return 0;
}
