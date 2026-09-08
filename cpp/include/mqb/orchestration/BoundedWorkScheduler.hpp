#pragma once

#include <cstddef>
#include <expected>
#include <functional>
#include <string>
#include <stop_token>

#include "mqb/orchestration/ParallelismPolicy.hpp"

namespace mqb::orchestration {

enum class BoundedWorkErrorCode {
    invalid_worker_count,
    callback_threw,
    worker_start_failed,
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
    // External admission stop observed before deregistration; distinct from a
    // false callback result. Never implies process or filesystem quiescence.
    bool admission_stop_observed{false};
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

    // Opt-in stop-admission / natural-drain boundary. Closing admission is
    // linearized by the registered stop callback, not by wall-clock callback
    // entry. An index already admitted may enter work after stop is requested.
    // All admitted callbacks finish (or throw) and all workers join before
    // return. Work must own its completion; detached work is NOT drained here.
    // No process token/Job, hard termination, bounded latency or write-lease
    // safety is implied. A non-stoppable token delegates to the legacy run.
    // Valid empty batches are no-ops; already-cancelled nonempty batches have
    // worker_count/started_count zero. Otherwise worker_count is the resolved
    // logical ceiling, not a count of necessarily-created background threads.
    [[nodiscard]] static std::expected<BoundedWorkSummary, BoundedWorkError>
    run_with_admission_stop(
        std::size_t item_count,
        std::size_t max_workers,
        std::stop_token admission_stop,
        const std::function<bool(std::size_t)>& work);

    [[nodiscard]] static std::expected<BoundedWorkSummary, BoundedWorkError>
    run_with_admission_stop(
        std::size_t item_count,
        ParallelismPolicy policy,
        ParallelismWorkload workload,
        std::stop_token admission_stop,
        const std::function<bool(std::size_t)>& work);
};

} // namespace mqb::orchestration
