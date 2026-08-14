#include "mqb/orchestration/BoundedWorkScheduler.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <expected>
#include <functional>
#include <thread>
#include <utility>
#include <vector>

namespace mqb::orchestration {
namespace {

[[nodiscard]] BoundedWorkError failure(
    const BoundedWorkErrorCode code,
    std::string message) {
    return BoundedWorkError{
        .code = code,
        .message = std::move(message),
    };
}

} // namespace

std::expected<BoundedWorkSummary, BoundedWorkError>
BoundedWorkScheduler::run(
    const std::size_t item_count,
    const std::size_t max_workers,
    const std::function<bool(std::size_t)>& work) {
    if (max_workers == 0) {
        return std::unexpected(failure(
            BoundedWorkErrorCode::invalid_worker_count,
            "bounded work scheduler requires at least one worker"));
    }

    if (item_count == 0) {
        return BoundedWorkSummary{};
    }

    const std::size_t worker_count = std::min(item_count, max_workers);
    if (worker_count == 1) {
        std::size_t started_count = 0;
        for (std::size_t index = 0; index < item_count; ++index) {
            ++started_count;
            bool keep_running = false;
            try {
                keep_running = work(index);
            } catch (...) {
                return std::unexpected(failure(
                    BoundedWorkErrorCode::callback_threw,
                    "bounded work callback threw an exception"));
            }
            if (!keep_running) {
                return BoundedWorkSummary{
                    .worker_count = 1,
                    .started_count = started_count,
                    .stop_requested = true,
                    .stopped_before_all_items = started_count < item_count,
                };
            }
        }
        return BoundedWorkSummary{
            .worker_count = 1,
            .started_count = started_count,
            .stop_requested = false,
            .stopped_before_all_items = false,
        };
    }

    std::atomic<std::size_t> next_index{0};
    std::atomic<std::size_t> started_count{0};
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> callback_threw{false};

    // Quench future dispatch before publishing the stop flag. A worker that
    // already observed stop_requested == false but has not claimed an index yet
    // will receive item_count (or greater) and exit without starting new work.
    const auto request_stop = [&] {
        next_index.exchange(item_count, std::memory_order_acq_rel);
        stop_requested.store(true, std::memory_order_release);
    };

    const auto worker_loop = [&] {
        while (!stop_requested.load(std::memory_order_acquire)) {
            const std::size_t index = next_index.fetch_add(1, std::memory_order_relaxed);
            if (index >= item_count) {
                return;
            }

            started_count.fetch_add(1, std::memory_order_relaxed);
            try {
                if (!work(index)) {
                    request_stop();
                }
            } catch (...) {
                callback_threw.store(true, std::memory_order_release);
                request_stop();
            }
        }
    };

    // The caller is already an available execution context. Count it as one of
    // the logical workers instead of creating worker_count background threads
    // and immediately parking the caller in join(). This preserves the exact
    // concurrency ceiling while removing one thread creation/destruction from
    // every parallel dispatch (including every named-module dependency level).
    std::vector<std::thread> background_workers;
    background_workers.reserve(worker_count - 1);
    for (std::size_t worker_index = 1; worker_index < worker_count; ++worker_index) {
        background_workers.emplace_back(worker_loop);
    }

    worker_loop();

    for (auto& worker : background_workers) {
        worker.join();
    }

    if (callback_threw.load(std::memory_order_acquire)) {
        return std::unexpected(failure(
            BoundedWorkErrorCode::callback_threw,
            "bounded work callback threw an exception"));
    }

    const std::size_t started = started_count.load(std::memory_order_relaxed);
    return BoundedWorkSummary{
        .worker_count = worker_count,
        .started_count = started,
        .stop_requested = stop_requested.load(std::memory_order_acquire),
        .stopped_before_all_items = started < item_count,
    };
}

std::expected<BoundedWorkSummary, BoundedWorkError>
BoundedWorkScheduler::run(
    const std::size_t item_count,
    const ParallelismPolicy policy,
    const std::function<bool(std::size_t)>& work) {
    if (!policy.valid()) {
        return std::unexpected(failure(
            BoundedWorkErrorCode::invalid_worker_count,
            "fixed parallelism policy requires at least one worker"));
    }
    if (item_count == 0) {
        return BoundedWorkSummary{};
    }

    const ParallelismDecision decision = ParallelismResolver::resolve(
        policy,
        item_count,
        std::thread::hardware_concurrency());
    return run(item_count, decision.workers, work);
}

} // namespace mqb::orchestration
