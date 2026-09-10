#pragma once

#include <cstddef>
#include <exception>
#include <expected>
#include <functional>
#include <optional>
#include <stop_token>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "mqb/orchestration/BoundedWorkScheduler.hpp"

namespace mqb::orchestration {

namespace detail {
template<class> inline constexpr bool is_expected_work_result = false;
template<class T, class E>
inline constexpr bool is_expected_work_result<std::expected<T, E>> = true;
} // namespace detail

template<class Result>
concept ExpectedWorkResult = detail::is_expected_work_result<Result>
    && std::is_nothrow_move_constructible_v<Result>;

enum class WorkBatchOutcome { succeeded, failed, cancelled };

// monostate means NOT ENTERED, never a successful or cancelled tool execution.
// Each admitted callback owns one preallocated slot. No slot is read while the
// scheduler is running. Original error/result payloads and exception_ptr survive
// cancellation, including when another callback or worker startup also fails.
template<ExpectedWorkResult Result>
struct WorkBatchReport {
    using Attempt = std::variant<std::monostate, Result, std::exception_ptr>;
    std::size_t requested_count{};
    std::vector<Attempt> attempts;
    std::optional<std::expected<BoundedWorkSummary, BoundedWorkError>> scheduling;
    // Allocation/setup or a thrown scheduler invocation. Already saved attempts
    // are retained. Absence of a scheduling summary is not successful drainage.
    std::exception_ptr dispatch_exception;

    [[nodiscard]] WorkBatchOutcome outcome() const noexcept {
        if (dispatch_exception || !scheduling || !*scheduling
            || attempts.size() != requested_count) return WorkBatchOutcome::failed;
        std::size_t completed = 0;
        for (const auto& attempt : attempts) {
            if (std::holds_alternative<std::monostate>(attempt)) continue;
            const auto* result = std::get_if<Result>(&attempt);
            if (!result || !result->has_value()) return WorkBatchOutcome::failed;
            ++completed;
        }
        const auto& summary = **scheduling;
        if (completed != summary.started_count) return WorkBatchOutcome::failed;
        // Failure above wins over external stop, even when stop was requested
        // before the failed callback returned. A late stop after deregistration
        // cannot mutate this report. Empty work follows the scheduler's no-op.
        if (summary.admission_stop_observed) return WorkBatchOutcome::cancelled;
        return completed == requested_count && !summary.stop_requested
            && !summary.stopped_before_all_items
            ? WorkBatchOutcome::succeeded : WorkBatchOutcome::failed;
    }

    // Necessary predecessor-result gate only. NOT freshness, transaction commit,
    // process-tree quiescence, or permission to transfer a project write lease.
    [[nodiscard]] bool all_succeeded() const noexcept {
        return outcome() == WorkBatchOutcome::succeeded;
    }
};

// Explicit opt-in result-preserving adapter, not a second scheduler. The work
// callable must return std::expected<T,E> by value. Nothrow move preserves the
// original payload without risking a second exception while storing it. Errors
// are never interpreted as cancellation; only scheduler admission observation
// produces the cancelled batch outcome. Admitted work owns its synchronous end.
// This API launches no process, passes no token to work and publishes no cache.
// Existing scheduler/CLI callers are unchanged. Work may run concurrently.
template<class Work, class Result = std::invoke_result_t<Work&, std::size_t>>
    requires ExpectedWorkResult<Result>
[[nodiscard]] WorkBatchReport<Result> run_work_batch(
    std::size_t item_count, std::size_t max_workers,
    std::stop_token admission_stop, Work&& work) {
    WorkBatchReport<Result> report;
    report.requested_count = item_count;
    try {
        // Always use the owned-worker, natural-drain path, including when the
        // caller supplies no token. The legacy non-stoppable path intentionally
        // remains unchanged; it is not this adapter's exception-safety boundary.
        std::stop_source private_lifetime;
        const auto token = admission_stop.stop_possible()
            ? admission_stop : private_lifetime.get_token();
        if (max_workers != 0) report.attempts.resize(item_count);
        report.scheduling.emplace(BoundedWorkScheduler::run_with_admission_stop(
            item_count, max_workers, token, [&](std::size_t index) {
                auto& slot = report.attempts[index];
                try {
                    slot.template emplace<1>(std::invoke(work, index));
                    return std::get<1>(slot).has_value();
                } catch (...) {
                    slot.template emplace<2>(std::current_exception());
                    throw; // Retain the scheduler's callback_threw error as well.
                }
            }));
    } catch (...) {
        // The chosen scheduler path owns and joins every started worker before
        // throwing out of its invocation. This does not cover detached/external
        // work, process errors or owner death, and implies no writer-lease safety.
        report.dispatch_exception = std::current_exception();
    }
    return report;
}

} // namespace mqb::orchestration
