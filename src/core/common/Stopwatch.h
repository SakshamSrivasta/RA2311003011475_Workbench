// Monotonic wall-clock timer used for all reported timings.
#pragma once

#include <chrono>

namespace wb {

class Stopwatch {
public:
    using Clock = std::chrono::steady_clock;

    Stopwatch() : start_(Clock::now()) {}

    void restart() { start_ = Clock::now(); }

    double elapsedSeconds() const {
        return std::chrono::duration<double>(Clock::now() - start_).count();
    }

private:
    Clock::time_point start_;
};

}  // namespace wb
