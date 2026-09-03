#include "mqb/orchestration/MsvcModuleTargetCoordinator.hpp"

#include <chrono>
#include <expected>
#include <string>
#include <utility>

#include "ModuleTargetLinkRequestFactory.hpp"
#include "ModuleTargetPreparation.hpp"

namespace mqb::orchestration {
namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] IncrementalModuleTargetError failure(
    const IncrementalModuleTargetErrorCode code,
    std::string message,
    std::filesystem::path source = {}) {
    return IncrementalModuleTargetError{
        .code = code,
        .message = std::move(message),
        .source = std::move(source),
    };
}

[[nodiscard]] IncrementalModuleTargetError compile_failure(
    std::string message,
    ModuleCompileError error) {
    IncrementalModuleTargetError result = failure(
        IncrementalModuleTargetErrorCode::compile_failed,
        std::move(message),
        error.source);
    result.compile_error = std::move(error);
    return result;
}

[[nodiscard]] IncrementalModuleTargetError link_failure(
    std::string message,
    IncrementalLinkError error) {
    IncrementalModuleTargetError result = failure(
        IncrementalModuleTargetErrorCode::link_failed,
        std::move(message));
    result.link_error = std::move(error);
    return result;
}

} // namespace

std::expected<IncrementalModuleTargetInspection, IncrementalModuleTargetError>
MsvcModuleTargetCoordinator::inspect_compilation(
    const IncrementalModuleTargetRequest& request) const {
    auto preparation = detail::inspect_module_target_preparation(
        request,
        scanner_);
    if (!preparation) {
        return std::unexpected(std::move(preparation.error()));
    }

    IncrementalModuleTargetInspection result;
    result.scans = std::move(preparation->scans);
    if (!preparation->prepared) {
        // At least one required P1689 document is stale or missing. The scan
        // recipes above are the complete honest compile-side model; provider
        // graph and compile waves depend on their output.
        return result;
    }

    detail::ModuleTargetPreparation prepared =
        std::move(*preparation->prepared);
    auto compiled = compile_coordinator_.inspect(prepared.compile_request);
    if (!compiled) {
        return std::unexpected(compile_failure(
            "module target compile-wave inspection failed",
            std::move(compiled.error())));
    }

    result.plan = std::move(prepared.plan);
    result.compile_request = std::move(prepared.compile_request);
    result.compiles = std::move(*compiled);
    return result;
}

std::expected<IncrementalModuleTargetInspection, IncrementalModuleTargetError>
MsvcModuleTargetCoordinator::inspect(
    const IncrementalModuleTargetRequest& request) const {
    auto result = inspect_compilation(request);
    if (!result) {
        return std::unexpected(std::move(result.error()));
    }
    if (!result->graph_ready()) {
        // Final link identity cannot be modeled until the provider graph and
        // its object-producing compile requests are trustworthy.
        return result;
    }
    if (!result->compile_request || !result->compiles) {
        return std::unexpected(failure(
            IncrementalModuleTargetErrorCode::compile_failed,
            "module target compilation inspection completed without its typed request/result"));
    }

    IncrementalLinkRequest link_request = detail::make_module_target_link_request(
        request,
        *result->compile_request,
        result->compiles->any_planned);
    auto linked = link_coordinator_.inspect(link_request);
    if (!linked) {
        return std::unexpected(link_failure(
            "module target link inspection failed",
            std::move(linked.error())));
    }

    result->link_request = std::move(link_request);
    result->link = std::move(*linked);
    return result;
}

std::expected<IncrementalModuleTargetResult, IncrementalModuleTargetError>
MsvcModuleTargetCoordinator::run(const IncrementalModuleTargetRequest& request) const {
    auto prepared = detail::prepare_module_target(request, scanner_);
    if (!prepared) {
        return std::unexpected(std::move(prepared.error()));
    }

    IncrementalModuleTargetResult result;
    result.timings = prepared->timings;
    result.scans = std::move(prepared->scans);
    result.plan = std::move(prepared->plan);

    const auto compile_started = Clock::now();
    auto compiled = compile_coordinator_.run(prepared->compile_request);
    result.timings.compile = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - compile_started);
    if (!compiled) {
        return std::unexpected(compile_failure(
            "module target compile waves failed",
            std::move(compiled.error())));
    }
    result.compiles = std::move(*compiled);

    const auto link_started = Clock::now();
    auto linked = link_coordinator_.run(detail::make_module_target_link_request(
        request,
        prepared->compile_request,
        result.compiles.any_compiled));
    result.timings.link = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - link_started);
    if (!linked) {
        return std::unexpected(link_failure(
            "module target link failed",
            std::move(linked.error())));
    }
    result.link = std::move(*linked);
    return result;
}

} // namespace mqb::orchestration
