#include "mqb/orchestration/MsvcModuleTargetCoordinator.hpp"

#include <algorithm>
#include <cctype>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "mqb/modules/ModuleDependencyGraph.hpp"
#include "mqb/msvc/MsvcModuleDependencyScanner.hpp"
#include "mqb/orchestration/BoundedWorkScheduler.hpp"

namespace mqb::orchestration {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string windows_path_key(const fs::path& path) {
    std::string value = path.lexically_normal().generic_string();
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

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

[[nodiscard]] std::optional<fs::path> working_directory_for(
    const fs::path& requested,
    const fs::path& source) {
    if (!requested.empty()) {
        return requested;
    }
    if (!source.parent_path().empty()) {
        return source.parent_path();
    }
    return std::nullopt;
}

} // namespace

std::expected<IncrementalModuleTargetResult, IncrementalModuleTargetError>
MsvcModuleTargetCoordinator::run(const IncrementalModuleTargetRequest& request) const {
    if (request.sources.empty()) {
        return std::unexpected(failure(
            IncrementalModuleTargetErrorCode::no_sources,
            "module target requires at least one source"));
    }
    if (request.max_parallel_scans == 0 || request.max_parallel_compiles == 0) {
        return std::unexpected(failure(
            IncrementalModuleTargetErrorCode::invalid_parallelism,
            "module target scan and compile parallelism must both be at least one"));
    }

    std::unordered_set<std::string> seen_sources;
    std::unordered_set<std::string> seen_scan_outputs;
    seen_sources.reserve(request.sources.size());
    seen_scan_outputs.reserve(request.sources.size());
    for (const auto& source : request.sources) {
        if (!seen_sources.emplace(windows_path_key(source.source)).second) {
            return std::unexpected(failure(
                IncrementalModuleTargetErrorCode::duplicate_source,
                "module target contains the same source more than once",
                source.source));
        }
        if (!seen_scan_outputs.emplace(
                windows_path_key(source.artifacts.module_dependencies)).second) {
            return std::unexpected(failure(
                IncrementalModuleTargetErrorCode::duplicate_scan_output,
                "two module target sources map to the same scan metadata output",
                source.source));
        }
    }

    using ScanAttempt = std::expected<msvc::ModuleScanResult, msvc::ModuleScanError>;
    std::vector<std::optional<ScanAttempt>> attempts(request.sources.size());

    const auto scheduled = BoundedWorkScheduler::run(
        request.sources.size(),
        request.max_parallel_scans,
        [&](const std::size_t index) {
            const auto& source = request.sources[index];
            msvc::ModuleScanInvocation invocation{
                .source = source.source,
                .output_file = source.artifacts.module_dependencies,
                .options = request.compiler_options,
                .kind = source.kind,
                .working_directory = working_directory_for(
                    request.working_directory,
                    source.source),
            };
            attempts[index].emplace(scanner_.scan(invocation));
            return attempts[index]->has_value();
        });
    if (!scheduled) {
        return std::unexpected(failure(
            IncrementalModuleTargetErrorCode::scheduling_failed,
            "module scan scheduler failed: " + scheduled.error().message));
    }

    // After all scan workers join, report the lowest request-index failure so
    // diagnostics do not depend on which concurrent process completed first.
    for (std::size_t index = 0; index < attempts.size(); ++index) {
        if (!attempts[index]) {
            continue;
        }
        if (!attempts[index]->has_value()) {
            IncrementalModuleTargetError error = failure(
                IncrementalModuleTargetErrorCode::scan_failed,
                "module dependency scan failed",
                request.sources[index].source);
            error.scan_error = attempts[index]->error();
            return std::unexpected(std::move(error));
        }
    }

    IncrementalModuleTargetResult result;
    result.scans.reserve(request.sources.size());
    std::vector<modules::ScannedModuleUnit> scanned_units;
    scanned_units.reserve(request.sources.size());

    for (std::size_t index = 0; index < request.sources.size(); ++index) {
        if (!attempts[index]) {
            return std::unexpected(failure(
                IncrementalModuleTargetErrorCode::scheduling_failed,
                "module scan scheduler stopped without a recorded scan failure",
                request.sources[index].source));
        }

        auto scan = std::move(attempts[index]->value());
        if (scan.dependencies.rules.size() != 1) {
            return std::unexpected(failure(
                IncrementalModuleTargetErrorCode::invalid_scan_result,
                "one-source module scan must produce exactly one P1689 rule",
                request.sources[index].source));
        }

        scanned_units.push_back(modules::ScannedModuleUnit{
            .source = request.sources[index].source,
            .rule = scan.dependencies.rules.front(),
        });
        result.scans.push_back(ModuleTargetScanResult{
            .source = request.sources[index].source,
            .result = std::move(scan),
        });
    }

    auto plan = modules::ModuleDependencyGraphBuilder::build(scanned_units);
    if (!plan) {
        IncrementalModuleTargetError error = failure(
            IncrementalModuleTargetErrorCode::graph_failed,
            "failed to build module dependency graph");
        error.graph_error = plan.error();
        return std::unexpected(std::move(error));
    }
    result.plan = *plan;

    ModuleCompileWaveRequest compile_request{
        .sources = request.sources,
        .plan = result.plan,
        .compiler_options = request.compiler_options,
        .working_directory = request.working_directory,
        .max_parallel_compiles = request.max_parallel_compiles,
    };
    auto compiled = compile_coordinator_.run(compile_request);
    if (!compiled) {
        IncrementalModuleTargetError error = failure(
            IncrementalModuleTargetErrorCode::compile_failed,
            "module target compile waves failed",
            compiled.error().source);
        error.compile_error = compiled.error();
        return std::unexpected(std::move(error));
    }
    result.compiles = std::move(*compiled);

    std::vector<fs::path> objects;
    objects.reserve(request.sources.size());
    for (const auto& source : request.sources) {
        objects.push_back(source.artifacts.object);
    }

    IncrementalLinkRequest link_request{
        .objects = std::move(objects),
        .output = request.target.executable,
        .options = request.link_options,
        .cache_file = request.target.link_cache,
        .working_directory = request.working_directory.empty()
            ? std::nullopt
            : std::optional<fs::path>{request.working_directory},
        .force_relink = result.compiles.any_compiled,
    };
    auto linked = link_coordinator_.run(link_request);
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
