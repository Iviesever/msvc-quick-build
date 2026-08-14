#pragma once

#include <cstddef>
#include <expected>
#include <functional>
#include <string>

#include "mqb/orchestration/ParallelismPolicy.hpp"

namespace mqb::orchestration {

enum class BoundedWorkErrorCode {
    invalid_worker_count,
    callback_threw,
};

struct BoundedWorkError {
    BoundedWorkErrorCode code{BoundedWorkErrorCode::invalid_worker_count};
    std::string message;
};

struct BoundedWorkSummary {
    std::size_t worker_count{};
    std::size_t started_count{};
    bool stop_requested{false};
    bool stopped_before_all_items{false};
};

class BoundedWorkScheduler {
public:
    // Work indices are assigned monotonically. Returning false requests that no
    // new indices be assigned; already assigned callbacks are allowed to finish.
    // The call returns only after every worker has joined.
    [[nodiscard]] static std::expected<BoundedWorkSummary, BoundedWorkError>
    run(
        std::size_t item_count,
        std::size_t max_workers,
        const std::function<bool(std::size_t)>& work);

    // Compatibility overload. Existing callers remain compilation workloads.
    [[nodiscard]] static std::expected<BoundedWorkSummary, BoundedWorkError>
    run(
        std::size_t item_count,
        ParallelismPolicy policy,
        const std::function<bool(std::size_t)>& work);

    // Automatic policy is resolved at each ready batch against both CPU width
    // and the current platform resource snapshot. Fixed -j N remains a user
    // ceiling and deliberately bypasses automatic resource adaptation.
    [[nodiscard]] static std::expected<BoundedWorkSummary, BoundedWorkError>
    run(
        std::size_t item_count,
        ParallelismPolicy policy,
        ParallelismWorkload workload,
        const std::function<bool(std::size_t)>& work);
};

} // namespace mqb::orchestration
