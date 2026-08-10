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
    std::atomic<std::size_t> next_index{0};
    std::atomic<std::size_t> started_count{0};
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> callback_threw{false};

    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (std::size_t worker_index = 0; worker_index < worker_count; ++worker_index) {
        workers.emplace_back([&] {
            while (!stop_requested.load(std::memory_order_acquire)) {
                const std::size_t index = next_index.fetch_add(1, std::memory_order_relaxed);
                if (index >= item_count) {
                    return;
                }

                started_count.fetch_add(1, std::memory_order_relaxed);
                try {
                    if (!work(index)) {
                        stop_requested.store(true, std::memory_order_release);
                    }
                } catch (...) {
                    callback_threw.store(true, std::memory_order_release);
                    stop_requested.store(true, std::memory_order_release);
                }
            }
        });
    }

    for (auto& worker : workers) {
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

} // namespace mqb::orchestration
