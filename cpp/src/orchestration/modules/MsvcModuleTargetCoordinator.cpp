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

} // namespace

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
        IncrementalModuleTargetError error = failure(
            IncrementalModuleTargetErrorCode::compile_failed,
            "module target compile waves failed",
            compiled.error().source);
        error.compile_error = compiled.error();
        return std::unexpected(std::move(error));
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
        IncrementalModuleTargetError error = failure(
            IncrementalModuleTargetErrorCode::link_failed,
            "module target link failed");
        error.link_error = linked.error();
        return std::unexpected(std::move(error));
    }
    result.link = std::move(*linked);
    return result;
}

} // namespace mqb::orchestration
