#include "mqb/orchestration/MsvcModuleTargetCoordinator.hpp"

#include <expected>
#include <string>
#include <utility>

#include "ModuleTargetLinkRequestFactory.hpp"
#include "ModuleTargetPreparation.hpp"

namespace mqb::orchestration {
namespace {

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
    result.scans = std::move(prepared->scans);
    result.plan = std::move(prepared->plan);

    auto compiled = compile_coordinator_.run(prepared->compile_request);
    if (!compiled) {
        IncrementalModuleTargetError error = failure(
            IncrementalModuleTargetErrorCode::compile_failed,
            "module target compile waves failed",
            compiled.error().source);
        error.compile_error = compiled.error();
        return std::unexpected(std::move(error));
    }
    result.compiles = std::move(*compiled);

    auto linked = link_coordinator_.run(detail::make_module_target_link_request(
        request,
        prepared->compile_request,
        result.compiles.any_compiled));
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
