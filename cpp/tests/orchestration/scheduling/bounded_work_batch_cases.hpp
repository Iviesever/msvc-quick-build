#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <expected>
#include <iostream>
#include <limits>
#include <memory>
#include <semaphore>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>

#include "mqb/orchestration/BoundedWorkBatch.hpp"

namespace mqb::tests {
inline int bounded_work_batch_cases() {
    using namespace std::chrono_literals;
    using namespace mqb::orchestration;
    struct Error { int code; std::string stdout_text, stderr_text; };
    using Result = std::expected<std::string, Error>;
    using Report = WorkBatchReport<Result>;
    int failures = 0;
    unsigned checks = 0;
    const auto check = [&](bool condition, const char* message) {
        ++checks;
        if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
    };
    const auto good = [](std::size_t i) -> Result { return "result-" + std::to_string(i); };
    {
        std::stop_source stop; stop.request_stop();
        const auto empty = run_work_batch(0, 2, stop.get_token(), good);
        check(empty.all_succeeded() && empty.attempts.empty(), "empty batch is an explicit no-op");
        const auto invalid = run_work_batch(3, 0, stop.get_token(), good);
        check(invalid.outcome() == WorkBatchOutcome::failed && invalid.scheduling && !*invalid.scheduling
              && invalid.scheduling->error().code == BoundedWorkErrorCode::invalid_worker_count,
              "pre-stop cannot erase invalid worker count");
        const auto cancelled = run_work_batch(4, 2, stop.get_token(), good);
        check(cancelled.outcome() == WorkBatchOutcome::cancelled && !cancelled.all_succeeded(), "pre-stop is cancellation");
        check(cancelled.attempts.size() == 4 && std::holds_alternative<std::monostate>(cancelled.attempts[0])
              && std::holds_alternative<std::monostate>(cancelled.attempts[3]), "unentered items remain explicit");
    }
    {
        auto complete = run_work_batch(7, 3, {}, good);
        check(complete.all_succeeded() && complete.attempts.size() == 7, "no-token typed work completes via owned scheduler");
        for (std::size_t i = 0; i < 7; ++i) {
            const auto* r = std::get_if<Result>(&complete.attempts[i]);
            check(r && *r && **r == "result-" + std::to_string(i), "completed result remains in its source slot");
        }
        // Incomplete/contradictory records must never become an all-success gate.
        complete.attempts[0].template emplace<0>();
        check(!complete.all_succeeded(), "missing attempt is not a success");
        Report unset;
        check(!unset.all_succeeded(), "missing scheduler record is not an empty successful batch");
    }
    {
        const auto failed = run_work_batch(6, 1, {}, [&](std::size_t i) -> Result {
            if (i == 1) return std::unexpected(Error{42, "native stdout\n", "native stderr\n"});
            return good(i);
        });
        const auto* original = std::get_if<Result>(&failed.attempts[1]);
        check(failed.outcome() == WorkBatchOutcome::failed && !failed.all_succeeded(), "original returned error fails batch");
        check(original && !*original && original->error().code == 42
              && original->error().stdout_text == "native stdout\n" && original->error().stderr_text == "native stderr\n",
              "original error and both diagnostics retained verbatim");
        check(std::holds_alternative<std::monostate>(failed.attempts[2]), "failure suppresses unadmitted work");
        check(failed.scheduling && *failed.scheduling && !(*failed.scheduling)->admission_stop_observed,
              "callback error is not external cancellation");
    }
    for (int mode = 0; mode < 3; ++mode) {
        // Two genuinely running callbacks; stop while both are admitted, then
        // let the delayed original error/exception emerge. No timing sleeps.
        std::stop_source stop;
        std::counting_semaphore<2> entered{0}, release{0};
        std::atomic<bool> returned{false};
        std::optional<Report> report;
        std::jthread caller([&] {
            report = run_work_batch(5, 2, stop.get_token(), [&](std::size_t index) -> Result {
                if (index < 2) { entered.release(); release.acquire(); }
                if (index == 1 && mode == 1) return std::unexpected(Error{2, "compiler out", "original compiler failure"});
                if (index == 1 && mode == 2) throw std::runtime_error("original callback exception");
                return good(index);
            });
            returned.store(true);
        });
        const bool first = entered.try_acquire_for(10s), second = entered.try_acquire_for(10s);
        check(first && second, "two callbacks reach fixed synchronization boundary");
        stop.request_stop();
        check(!returned.load(), "admission stop does not return before active callbacks drain");
        release.release(2); caller.join();
        check(report.has_value() && !report->all_succeeded(), "cancel/error prevents successor phase");
        check(report && report->outcome() == (mode == 0 ? WorkBatchOutcome::cancelled : WorkBatchOutcome::failed),
              "original errors/exceptions take precedence over simultaneous stop");
        if (report) {
            const auto* first_result = std::get_if<Result>(&report->attempts[0]);
            check(first_result && *first_result && **first_result == "result-0", "successful sibling result survives failed batch");
            check(std::holds_alternative<std::monostate>(report->attempts[2]), "pending index is never successful");
            if (mode == 1) {
                const auto* r = std::get_if<Result>(&report->attempts[1]);
                check(r && !*r && r->error().stderr_text == "original compiler failure", "delayed compiler error retained after stop");
            }
            if (mode == 2) {
                const auto* ptr = std::get_if<std::exception_ptr>(&report->attempts[1]);
                std::string text;
                if (ptr && *ptr) try { std::rethrow_exception(*ptr); } catch (const std::runtime_error& e) { text = e.what(); }
                check(text == "original callback exception", "original exception remains rethrowable");
                check(report->scheduling && !*report->scheduling
                      && report->scheduling->error().code == BoundedWorkErrorCode::callback_threw,
                      "scheduler exception metadata also retained");
            }
        }
    }
    {
        std::stop_source stop;
        const auto finished = run_work_batch(2, 1, stop.get_token(), good);
        stop.request_stop();
        check(finished.all_succeeded(), "stop after deregistration cannot mutate completed report");
        std::stop_source last;
        const auto cancelled = run_work_batch(1, 1, last.get_token(), [&](std::size_t i) -> Result { last.request_stop(); return good(i); });
        check(cancelled.outcome() == WorkBatchOutcome::cancelled && std::get<Result>(cancelled.attempts[0]).has_value(),
              "all admitted results can succeed while external stop remains a distinct batch outcome");
    }
    {
        std::atomic<int> calls{0};
        const auto allocation = run_work_batch(std::numeric_limits<std::size_t>::max(), 2, {}, [&](std::size_t i) -> Result {
            ++calls; return good(i);
        });
        check(allocation.outcome() == WorkBatchOutcome::failed && allocation.dispatch_exception && calls.load() == 0,
              "result allocation failure retains exception and launches no callback");
    }
    {
        // Error/stop in A is not permission to stop independent batch B.
        std::stop_source a_stop, b_stop;
        std::counting_semaphore<2> entered{0}, release{0};
        std::optional<Report> a, b;
        const auto work = [&](std::size_t index) -> Result {
            if (index == 0) { entered.release(); release.acquire(); }
            return good(index);
        };
        std::jthread ta([&] { a = run_work_batch(3, 1, a_stop.get_token(), work); });
        std::jthread tb([&] { b = run_work_batch(3, 1, b_stop.get_token(), work); });
        const bool first = entered.try_acquire_for(10s), second = entered.try_acquire_for(10s);
        check(first && second, "independent batches entered");
        a_stop.request_stop(); release.release(2); ta.join(); tb.join();
        check(a && a->outcome() == WorkBatchOutcome::cancelled && b && b->all_succeeded() && !b_stop.stop_requested(),
              "independent result batches do not share cancellation");
    }
    {
        const auto movable = run_work_batch(2, 2, {}, [](std::size_t i) -> std::expected<std::unique_ptr<int>, int> {
            return std::make_unique<int>(static_cast<int>(i));
        });
        check(movable.all_succeeded(), "move-only expected payloads need no copy");
        const auto void_result = run_work_batch(1, 1, {}, [](std::size_t) -> std::expected<void, int> { return {}; });
        check(void_result.all_succeeded(), "void expected successes supported");
    }
    std::cout << "bounded_work_batch_cases " << checks << " checks " << (failures == 0 ? "passed" : "failed") << '\n';
    return failures;
}
} // namespace mqb::tests
