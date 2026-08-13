#include "mqb/orchestration/MsvcModuleTargetCoordinator.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ModuleTargetPreparation.hpp"

namespace mqb::orchestration {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] IncrementalModuleTargetError failure(
    const IncrementalModuleTargetErrorCode code,
    std::string message,
    fs::path source = {}) {
    return IncrementalModuleTargetError{
        .code = code,
        .message = std::move(message),
        .source = std::move(source),
    };
}

[[nodiscard]] IncrementalLinkRequest make_link_request(
    const IncrementalModuleTargetRequest& request,
    const ModuleCompileWaveRequest& compile_request,
    const bool force_relink) {
    std::vector<fs::path> objects;
    objects.reserve(compile_request.sources.size());
    for (const auto& source : compile_request.sources) {
        objects.push_back(source.artifacts.object);
    }

    return IncrementalLinkRequest{
        .objects = std::move(objects),
        .output = request.target.executable,
        .options = request.link_options,
        .cache_file = request.target.link_cache,
        .working_directory = request.working_directory.empty()
            ? std::nullopt
            : std::optional<fs::path>{request.working_directory},
        .force_relink = force_relink,
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

    auto linked = link_coordinator_.run(make_link_request(
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
