#pragma once

#include <chrono>

namespace mqb::orchestration {

struct TargetTimings {
    std::chrono::nanoseconds dependency_scan{};
    std::chrono::nanoseconds compile_queue{};
    std::chrono::nanoseconds compile{};
    std::chrono::nanoseconds link{};
    std::chrono::nanoseconds archive{};
};

} // namespace mqb::orchestration
