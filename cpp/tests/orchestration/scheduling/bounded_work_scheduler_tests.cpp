#include <atomic>
#include <barrier>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>
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
    using mqb::orchestration::ParallelismMode;
    using mqb::orchestration::ParallelismPolicy;
    using mqb::orchestration::ParallelismResolver;

    {
        const ParallelismPolicy automatic;
        expect(automatic.is_automatic() && automatic.valid(),
               "default parallelism policy should be valid automatic policy");

        const ParallelismPolicy numeric_compatibility = 4;
        expect(!numeric_compatibility.is_automatic()
                   && numeric_compatibility.fixed_jobs == 4
                   && numeric_compatibility == 4,
               "legacy numeric request initializer should become fixed policy");
        const ParallelismPolicy zero_compatibility = 0;
        expect(!zero_compatibility.valid() && zero_compatibility == 0,
               "legacy zero request initializer should remain invalid");
    }

    {
        const auto unknown_hardware = ParallelismResolver::resolve(
            ParallelismPolicy::automatic(), 8, 0);
        expect(unknown_hardware.workers == 1,
               "automatic policy should fall back to one worker when hardware count is unknown");

        const auto work_limited = ParallelismResolver::resolve(
            ParallelismPolicy::automatic(), 3, 16);
        expect(work_limited.workers == 3
                   && work_limited.mode == ParallelismMode::automatic,
               "automatic policy should clamp hardware budget to ready work width");

        const auto hardware_limited = ParallelismResolver::resolve(
            ParallelismPolicy::automatic(), 12, 4);
        expect(hardware_limited.workers == 4,
               "automatic policy should respect the available hardware budget");

        const auto fixed_work_limited = ParallelismResolver::resolve(
            ParallelismPolicy::fixed(9), 2, 64);
        expect(fixed_work_limited.workers == 2
                   && fixed_work_limited.mode == ParallelismMode::fixed,
               "fixed policy should still clamp to ready work width");

        const auto fixed_limit = ParallelismResolver::resolve(
            ParallelismPolicy::fixed(2), 12, 64);
        expect(fixed_limit.workers == 2,
               "fixed policy should remain a hard user ceiling regardless of hardware count");

        const auto no_work = ParallelismResolver::resolve(
            ParallelismPolicy::automatic(), 0, 16);
        expect(no_work.workers == 0,
               "zero ready work should resolve to zero workers");
    }

    {
        const auto invalid = BoundedWorkScheduler::run(4, 0, [](std::size_t) { return true; });
        expect(!invalid, "zero workers should be rejected");
        if (!invalid) {
            expect(invalid.error().code == BoundedWorkErrorCode::invalid_worker_count,
                   "zero workers should report invalid_worker_count");
        }

        const auto invalid_policy = BoundedWorkScheduler::run(
            4,
            ParallelismPolicy::fixed(0),
            [](std::size_t) { return true; });
        expect(!invalid_policy, "fixed zero policy should be rejected");
        if (!invalid_policy) {
            expect(invalid_policy.error().code == BoundedWorkErrorCode::invalid_worker_count,
                   "fixed zero policy should report invalid_worker_count");
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
        const auto caller_thread = std::this_thread::get_id();
        std::thread::id callback_thread;
        const auto sequential = BoundedWorkScheduler::run(
            6,
            1,
            [&](const std::size_t index) {
                callback_thread = std::this_thread::get_id();
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
        expect(callback_thread == caller_thread,
               "single-worker scheduling should execute inline without creating a worker thread");
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
        std::atomic<bool> caller_participated{false};
        const auto caller_thread = std::this_thread::get_id();
        std::vector<std::atomic<bool>> seen(5);
        for (auto& value : seen) value.store(false, std::memory_order_relaxed);

        const auto concurrent = BoundedWorkScheduler::run(
            seen.size(),
            concurrent_workers,
            [&](const std::size_t index) {
                if (std::this_thread::get_id() == caller_thread) {
                    caller_participated.store(true, std::memory_order_relaxed);
                }
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
        expect(caller_participated.load(std::memory_order_relaxed),
               "multi-worker scheduling should use the caller as one logical worker");
        expect(maximum_active.load(std::memory_order_relaxed) == 3,
               "three logical workers should be observably active at the same time");
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

    {
        const auto threw_inline = BoundedWorkScheduler::run(
            1,
            1,
            [](std::size_t) -> bool {
                throw std::runtime_error{"inline-scheduler-test"};
            });
        expect(!threw_inline,
               "inline single-worker callback exception should remain a scheduler error");
        if (!threw_inline) {
            expect(threw_inline.error().code == BoundedWorkErrorCode::callback_threw,
                   "inline callback exception should report callback_threw");
        }
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_bounded_work_scheduler_tests passed\n";
    return 0;
}
