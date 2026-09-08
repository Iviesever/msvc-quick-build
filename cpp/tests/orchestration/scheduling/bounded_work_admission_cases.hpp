#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <limits>
#include <optional>
#include <semaphore>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "mqb/orchestration/BoundedWorkScheduler.hpp"

namespace mqb::tests {

// Included by the existing scheduler test executable: preserve the native
// inventory/shard plan while exercising the new, explicitly opted-in path.
inline int bounded_work_admission_cases() {
    using namespace mqb::orchestration;
    using namespace std::chrono_literals;
    using Result = std::expected<BoundedWorkSummary, BoundedWorkError>;
    int failures = 0;
    const auto check = [&](bool ok, const char* message) {
        if (!ok) {
            ++failures;
            std::cerr << "FAIL admission: " << message << '\n';
        }
    };
    const auto no_work = [](std::size_t) { return true; };

    {
        std::stop_source stop;
        stop.request_stop();
        std::size_t calls = 0;
        const auto work = [&](std::size_t) { ++calls; return true; };
        const auto cancelled = BoundedWorkScheduler::run_with_admission_stop(
            20, 4, stop.get_token(), work);
        check(cancelled && cancelled->worker_count == 0 && cancelled->started_count == 0
                  && cancelled->stop_requested && cancelled->admission_stop_observed
                  && cancelled->stopped_before_all_items && calls == 0,
              "pre-cancelled batch must admit nothing and use no workers");
        const auto empty = BoundedWorkScheduler::run_with_admission_stop(
            0, 4, stop.get_token(), work);
        check(empty && empty->worker_count == 0 && !empty->stop_requested && calls == 0,
              "valid empty batch is a no-op even with a cancelled token");
        const auto invalid = BoundedWorkScheduler::run_with_admission_stop(
            0, 0, stop.get_token(), work);
        check(!invalid && invalid.error().code == BoundedWorkErrorCode::invalid_worker_count,
              "cancellation/empty work cannot hide invalid worker count");
        const auto invalid_policy = BoundedWorkScheduler::run_with_admission_stop(
            0, ParallelismPolicy::fixed(0), ParallelismWorkload::dependency_scan,
            stop.get_token(), work);
        check(!invalid_policy
                  && invalid_policy.error().code == BoundedWorkErrorCode::invalid_worker_count,
              "cancellation/empty work cannot hide invalid policy");
        const auto automatic = BoundedWorkScheduler::run_with_admission_stop(
            20, ParallelismPolicy::automatic(), ParallelismWorkload::dependency_scan,
            stop.get_token(), work);
        check(automatic && automatic->worker_count == 0 && automatic->admission_stop_observed,
              "pre-cancelled automatic batch must not dispatch");
    }
    {
        std::vector<std::size_t> order;
        const auto legacy = BoundedWorkScheduler::run_with_admission_stop(
            10, 1, {}, [&](std::size_t index) { order.push_back(index); return index != 2; });
        check(legacy && legacy->started_count == 3 && legacy->stop_requested
                  && !legacy->admission_stop_observed
                  && order == std::vector<std::size_t>{0, 1, 2},
              "non-stoppable token must preserve legacy false-callback behavior");
        const auto policy = BoundedWorkScheduler::run_with_admission_stop(
            3, ParallelismPolicy::fixed(2), ParallelismWorkload::compilation, {}, no_work);
        check(policy && policy->started_count == 3 && !policy->admission_stop_observed,
              "non-stoppable policy overload must preserve legacy behavior");
    }
    {
        std::stop_source stop;
        std::vector<std::size_t> order;
        const auto caller = std::this_thread::get_id();
        bool inline_only = true;
        const auto complete = BoundedWorkScheduler::run_with_admission_stop(
            6, 1, stop.get_token(), [&](std::size_t index) {
                inline_only &= std::this_thread::get_id() == caller;
                order.push_back(index);
                return true;
            });
        check(complete && complete->started_count == 6 && complete->worker_count == 1
                  && !complete->stop_requested && !complete->admission_stop_observed
                  && inline_only && order == std::vector<std::size_t>{0, 1, 2, 3, 4, 5},
              "uncancelled inline batch preserves monotonic order and caller execution");
        // Registration must have been removed; requesting stop after return is
        // safe and cannot retrospectively change the returned summary.
        check(stop.request_stop() && complete && !complete->admission_stop_observed,
              "late stop must not retain references to destroyed batch state");
    }
    {
        std::stop_source stop;
        const auto result = BoundedWorkScheduler::run_with_admission_stop(
            20, 1, stop.get_token(), [&](std::size_t index) {
                if (index == 2) stop.request_stop();
                return true;
            });
        check(result && result->started_count == 3 && result->stop_requested
                  && result->admission_stop_observed && result->stopped_before_all_items,
              "callback may request admission stop synchronously without deadlock");
        std::stop_source final_stop;
        const auto final = BoundedWorkScheduler::run_with_admission_stop(
            1, 1, final_stop.get_token(), [&](std::size_t) {
                final_stop.request_stop(); return true;
            });
        check(final && final->started_count == 1 && final->admission_stop_observed
                  && !final->stopped_before_all_items,
              "stop on final admitted task is not an incomplete batch");
    }
    {
        std::stop_source stop;
        std::counting_semaphore<3> entered{0};
        std::counting_semaphore<3> release{0};
        std::atomic<bool> returned{false};
        std::atomic<int> finished{0};
        std::array<std::string, 20> diagnostics;
        std::optional<Result> result;
        std::jthread schedule([&] {
            result = BoundedWorkScheduler::run_with_admission_stop(
                diagnostics.size(), 3, stop.get_token(), [&](std::size_t index) {
                    if (index < 3) { entered.release(); release.acquire(); }
                    diagnostics[index] = "complete-" + std::to_string(index);
                    finished.fetch_add(1);
                    return true;
                });
            returned.store(true);
        });
        bool ready = true;
        for (int i = 0; i < 3; ++i) ready &= entered.try_acquire_for(10s);
        check(ready, "three admitted tasks must reach controlled boundary");
        stop.request_stop();
        check(!returned.load() && finished.load() == 0,
              "stop request cannot release a batch while admitted callbacks are blocked");
        release.release(3);
        schedule.join();
        check(result && *result && (*result)->started_count == 3
                  && (*result)->admission_stop_observed && (*result)->stopped_before_all_items
                  && finished.load() == 3,
              "exactly the admitted wave must finish; pending work must not start");
        check(diagnostics[0] == "complete-0" && diagnostics[1] == "complete-1"
                  && diagnostics[2] == "complete-2" && diagnostics[3].empty(),
              "all admitted diagnostic writes must be visible when run returns");
    }
    {
        // Hold two independent batches at the same boundary. Cancelling A may
        // not quench B's remaining indices, even on the same scheduler API.
        std::stop_source a_stop, b_stop;
        std::counting_semaphore<2> entered{0}, release{0};
        std::optional<Result> a, b;
        std::array<int, 7> a_calls{}, b_calls{};
        const auto run = [&](std::stop_source& stop, std::array<int, 7>& calls,
                             std::optional<Result>& result) {
            result = BoundedWorkScheduler::run_with_admission_stop(
                calls.size(), 1, stop.get_token(), [&](std::size_t index) {
                    ++calls[index];
                    if (index == 0) { entered.release(); release.acquire(); }
                    return true;
                });
        };
        std::jthread ta([&] { run(a_stop, a_calls, a); });
        std::jthread tb([&] { run(b_stop, b_calls, b); });
        const bool first_ready = entered.try_acquire_for(10s);
        const bool second_ready = entered.try_acquire_for(10s);
        check(first_ready && second_ready, "both independent batches must enter");
        a_stop.request_stop();
        release.release(2);
        ta.join(); tb.join();
        check(a && *a && (*a)->started_count == 1 && (*a)->admission_stop_observed,
              "A must stop its own pending admissions");
        check(b && *b && (*b)->started_count == b_calls.size()
                  && !(*b)->stop_requested && !b_stop.stop_requested(),
              "A cancellation must leave B token and pending work intact");
        check(a_calls[0] == 1 && a_calls[1] == 0 && b_calls[6] == 1,
              "independent batch work identities must not leak");
    }
    {
        std::stop_source stop;
        const auto callback_stop = BoundedWorkScheduler::run_with_admission_stop(
            8, 1, stop.get_token(), [](std::size_t index) { return index != 2; });
        check(callback_stop && callback_stop->started_count == 3
                  && callback_stop->stop_requested && !callback_stop->admission_stop_observed,
              "false callback is distinct from external admission cancellation");
        const auto exception = BoundedWorkScheduler::run_with_admission_stop(
            8, 1, stop.get_token(), [&](std::size_t) -> bool {
                stop.request_stop(); throw std::runtime_error("retained failure");
            });
        check(!exception && exception.error().code == BoundedWorkErrorCode::callback_threw,
              "cancellation cannot erase an admitted callback exception");
    }
    {
        std::stop_source stop;
        std::binary_semaphore entered{0}, release{0};
        std::atomic<bool> returned{false}, finished{false};
        std::optional<Result> result;
        std::jthread schedule([&] {
            result = BoundedWorkScheduler::run_with_admission_stop(
                10, 2, stop.get_token(), [&](std::size_t index) -> bool {
                    if (index == 0) {
                        entered.release(); release.acquire(); finished.store(true);
                        return true;
                    }
                    stop.request_stop();
                    throw std::runtime_error("peer failure");
                });
            returned.store(true);
        });
        check(entered.try_acquire_for(10s), "first admitted callback must enter before failure drain");
        // Whether the throwing peer has finished yet or not, a blocked admitted
        // callback prevents both successful and erroneous scheduler return.
        check(!returned.load(), "callback error must not detach a still-running peer");
        release.release();
        schedule.join();
        check(result && !*result && result->error().code == BoundedWorkErrorCode::callback_threw
                  && finished.load(), "callback exception returns only after peer completion");
    }
    {
        std::stop_source stop;
        const auto enormous = BoundedWorkScheduler::run_with_admission_stop(
            std::numeric_limits<std::size_t>::max(), 1, stop.get_token(), [&](std::size_t index) {
                stop.request_stop(); return index == 0;
            });
        check(enormous && enormous->started_count == 1 && enormous->admission_stop_observed,
              "saturated admission must support SIZE_MAX item count without wrapping");
        std::stop_source allocation_stop;
        std::size_t calls = 0;
        const auto impossible = BoundedWorkScheduler::run_with_admission_stop(
            std::numeric_limits<std::size_t>::max(), std::numeric_limits<std::size_t>::max(),
            allocation_stop.get_token(), [&](std::size_t) { ++calls; return true; });
        check(!impossible && impossible.error().code == BoundedWorkErrorCode::worker_start_failed
                  && calls == 0, "impossible worker storage is an error, not process termination");
    }
    {
        std::stop_source stop;
        const auto fixed = BoundedWorkScheduler::run_with_admission_stop(
            6, ParallelismPolicy::fixed(2), ParallelismWorkload::dependency_scan,
            stop.get_token(), no_work);
        check(fixed && fixed->worker_count == 2 && fixed->started_count == 6,
              "fixed policy keeps its existing worker ceiling");
        const auto automatic = BoundedWorkScheduler::run_with_admission_stop(
            2, ParallelismPolicy::automatic(), ParallelismWorkload::compilation,
            stop.get_token(), no_work);
        check(automatic && automatic->worker_count >= 1 && automatic->worker_count <= 2
                  && automatic->started_count == 2,
              "automatic policy remains bounded by ready work");
    }
    return failures;
}

} // namespace mqb::tests
