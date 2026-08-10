#include <atomic>
#include <barrier>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "mqb/orchestration/BoundedWorkScheduler.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void update_max(std::atomic<int>& maximum, const int value) {
    int observed = maximum.load(std::memory_order_relaxed);
    while (observed < value
           && !maximum.compare_exchange_weak(
               observed,
               value,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

} // namespace

int main() {
    using mqb::orchestration::BoundedWorkErrorCode;
    using mqb::orchestration::BoundedWorkScheduler;

    {
        const auto invalid = BoundedWorkScheduler::run(4, 0, [](std::size_t) { return true; });
        expect(!invalid, "zero workers should be rejected");
        if (!invalid) {
            expect(invalid.error().code == BoundedWorkErrorCode::invalid_worker_count,
                   "zero workers should report invalid_worker_count");
        }
    }

    {
        const auto empty = BoundedWorkScheduler::run(0, 4, [](std::size_t) { return true; });
        expect(empty.has_value(), "zero work items should be a successful no-op");
        if (empty) {
            expect(empty->worker_count == 0 && empty->started_count == 0,
                   "zero work items should not create worker threads");
        }
    }

    {
        std::vector<std::size_t> order;
        const auto sequential = BoundedWorkScheduler::run(
            6,
            1,
            [&](const std::size_t index) {
                order.push_back(index);
                return true;
            });
        expect(sequential.has_value(), "single-worker scheduling should succeed");
        if (sequential) {
            expect(sequential->worker_count == 1 && sequential->started_count == 6,
                   "single-worker scheduling should process every item");
            expect(!sequential->stop_requested && !sequential->stopped_before_all_items,
                   "successful single-worker scheduling should not report early stop");
        }
        expect(order == std::vector<std::size_t>({0, 1, 2, 3, 4, 5}),
               "single-worker scheduling should preserve monotonic index order");
    }

    {
        std::vector<std::size_t> order;
        const auto stopped = BoundedWorkScheduler::run(
            6,
            1,
            [&](const std::size_t index) {
                order.push_back(index);
                return index != 2;
            });

        expect(stopped.has_value(), "single-worker callback stop should be a successful schedule");
        if (stopped) {
            expect(stopped->worker_count == 1 && stopped->started_count == 3,
                   "single-worker stop should deterministically truncate later dispatch");
            expect(stopped->stop_requested && stopped->stopped_before_all_items,
                   "single-worker stop should report an early stop");
        }
        expect(order == std::vector<std::size_t>({0, 1, 2}),
               "single-worker stop should not dispatch an index after the failing item");
    }

    {
        constexpr std::size_t concurrent_workers = 3;
        std::barrier gate{static_cast<std::ptrdiff_t>(concurrent_workers)};
        std::atomic<int> active{0};
        std::atomic<int> maximum_active{0};
        std::vector<std::atomic<bool>> seen(5);
        for (auto& value : seen) value.store(false, std::memory_order_relaxed);

        const auto concurrent = BoundedWorkScheduler::run(
            seen.size(),
            concurrent_workers,
            [&](const std::size_t index) {
                seen[index].store(true, std::memory_order_relaxed);
                const int now_active = active.fetch_add(1, std::memory_order_relaxed) + 1;
                update_max(maximum_active, now_active);
                if (index < concurrent_workers) {
                    gate.arrive_and_wait();
                }
                active.fetch_sub(1, std::memory_order_relaxed);
                return true;
            });

        expect(concurrent.has_value(), "multi-worker scheduling should succeed");
        if (concurrent) {
            expect(concurrent->worker_count == concurrent_workers,
                   "scheduler should respect the requested worker bound");
            expect(concurrent->started_count == seen.size(),
                   "successful concurrent scheduling should start every item");
        }
        expect(maximum_active.load(std::memory_order_relaxed) == 3,
               "three workers should be observably active at the same time");
        for (std::size_t index = 0; index < seen.size(); ++index) {
            expect(seen[index].load(std::memory_order_relaxed),
                   "every successful work index should run exactly once");
        }
    }

    {
        constexpr std::size_t worker_count = 3;
        std::barrier first_wave{static_cast<std::ptrdiff_t>(worker_count)};
        std::vector<std::atomic<int>> calls(20);
        for (auto& value : calls) value.store(0, std::memory_order_relaxed);
        std::atomic<int> finished{0};

        const auto stopped = BoundedWorkScheduler::run(
            calls.size(),
            worker_count,
            [&](const std::size_t index) {
                calls[index].fetch_add(1, std::memory_order_relaxed);
                if (index < worker_count) {
                    first_wave.arrive_and_wait();
                }
                finished.fetch_add(1, std::memory_order_relaxed);
                return index != 2;
            });

        expect(stopped.has_value(), "callback-requested stop should not be a scheduler error");
        if (stopped) {
            expect(stopped->worker_count == worker_count,
                   "stop path should preserve the requested worker bound");
            expect(stopped->stop_requested,
                   "false callback result should record a stop request");
            expect(stopped->started_count >= worker_count,
                   "the complete first wave must be joined after a failure");
            expect(stopped->stopped_before_all_items == (stopped->started_count < calls.size()),
                   "early-stop summary should match the actual number of started items");
            expect(finished.load(std::memory_order_relaxed)
                       == static_cast<int>(stopped->started_count),
                   "scheduler must join every callback that it started");
        }
        expect(calls[0].load(std::memory_order_relaxed) == 1
                   && calls[1].load(std::memory_order_relaxed) == 1
                   && calls[2].load(std::memory_order_relaxed) == 1,
               "monotonic assignment must not skip lower indices before a failing index");
        for (const auto& call_count : calls) {
            expect(call_count.load(std::memory_order_relaxed) <= 1,
                   "each work index must be assigned at most once");
        }
    }

    {
        const auto threw = BoundedWorkScheduler::run(
            4,
            2,
            [](const std::size_t index) -> bool {
                if (index == 0) throw std::runtime_error{"scheduler-test"};
                return true;
            });
        expect(!threw, "callback exception should become a scheduler error");
        if (!threw) {
            expect(threw.error().code == BoundedWorkErrorCode::callback_threw,
                   "callback exception should report callback_threw");
        }
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_bounded_work_scheduler_tests passed\n";
    return 0;
}
