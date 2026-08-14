#include "ModuleTargetPreparation.hpp"

#include <chrono>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ModuleTargetArtifactRegistry.hpp"
#include "ModuleTargetScanner.hpp"
#include "StandardLibraryModuleProvider.hpp"
#include "mqb/modules/ModuleDependencyGraph.hpp"

namespace mqb::orchestration::detail {
namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

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

[[nodiscard]] std::optional<HeaderUnitLookupMethod> core_lookup_method(
    const modules::LookupMethod lookup_method) {
    switch (lookup_method) {
    case modules::LookupMethod::include_angle:
        return HeaderUnitLookupMethod::angle;
    case modules::LookupMethod::include_quote:
        return HeaderUnitLookupMethod::quote;
    case modules::LookupMethod::by_name:
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace

std::expected<ModuleTargetPreparation, IncrementalModuleTargetError>
prepare_module_target(
    const IncrementalModuleTargetRequest& request,
    msvc::MsvcModuleDependencyScanner& scanner) {
    if (request.sources.empty()) {
        return std::unexpected(failure(
            IncrementalModuleTargetErrorCode::no_sources,
            "module target requires at least one source"));
    }
    if (!request.max_parallel_scans.valid() || !request.max_parallel_compiles.valid()) {
        return std::unexpected(failure(
            IncrementalModuleTargetErrorCode::invalid_parallelism,
            "module target scan and compile parallelism must be automatic or a positive fixed worker count"));
    }

    const auto preparation_started = Clock::now();

    ModuleTargetArtifactRegistry artifacts{request.sources.size()};
    for (const auto& source : request.sources) {
        if (auto registered = artifacts.add_requested_source(source); !registered) {
            return std::unexpected(std::move(registered.error()));
        }
    }
    if (auto registered = artifacts.add_target(request.target); !registered) {
        return std::unexpected(std::move(registered.error()));
    }

    const auto scan_started = Clock::now();
    auto scanned = scan_requested_module_sources(request, scanner);
    if (!scanned) {
        return std::unexpected(std::move(scanned.error()));
    }

    ModuleTargetPreparation prepared;
    prepared.scans = std::move(scanned->scans);
    std::vector<modules::ScannedModuleUnit> scanned_units = std::move(scanned->units);
    std::vector<ModuleCompileSourceRequest> compile_sources = request.sources;
    compile_sources.reserve(request.sources.size() + 2u);

    if (auto injected = inject_standard_library_module_providers(
            request,
            scanner,
            artifacts,
            scanned_units,
            compile_sources); !injected) {
        return std::unexpected(std::move(injected.error()));
    }
    prepared.timings.dependency_scan = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - scan_started);

    auto plan = modules::ModuleDependencyGraphBuilder::build(
        scanned_units,
        request.compiler_options.external_module_providers);
    if (!plan) {
        IncrementalModuleTargetError error = failure(
            IncrementalModuleTargetErrorCode::graph_failed,
            "failed to build module dependency graph");
        error.graph_error = plan.error();
        return std::unexpected(std::move(error));
    }
    prepared.plan = *plan;

    std::vector<ModuleCompileHeaderUnitRequest> header_units;
    header_units.reserve(prepared.plan.header_units.size());
    if (!prepared.plan.header_units.empty() && !request.artifact_layout) {
        return std::unexpected(failure(
            IncrementalModuleTargetErrorCode::artifact_layout_missing,
            "module target discovered project-local header units but no artifact layout was provided",
            prepared.plan.header_units.front().source));
    }

    for (const auto& header : prepared.plan.header_units) {
        const auto lookup = core_lookup_method(header.lookup_method);
        if (!lookup) {
            return std::unexpected(failure(
                IncrementalModuleTargetErrorCode::invalid_header_unit,
                "resolved header-unit provider has a non-header lookup method",
                header.source));
        }

        auto header_artifacts = request.artifact_layout->for_source(header.source);
        if (!header_artifacts) {
            IncrementalModuleTargetError error = failure(
                IncrementalModuleTargetErrorCode::artifact_layout_failed,
                "failed to assign artifacts to discovered header-unit provider",
                header.source);
            error.artifact_layout_error = header_artifacts.error();
            return std::unexpected(std::move(error));
        }

        if (auto registered = artifacts.add_header_unit_source(
                header.source,
                *header_artifacts); !registered) {
            return std::unexpected(std::move(registered.error()));
        }

        header_units.push_back(ModuleCompileHeaderUnitRequest{
            .source = header.source,
            .header_name = header.header_name,
            .lookup_method = *lookup,
            .artifacts = std::move(*header_artifacts),
        });
    }

    prepared.compile_request = ModuleCompileWaveRequest{
        .sources = std::move(compile_sources),
        .header_units = std::move(header_units),
        .plan = prepared.plan,
        .compiler_options = request.compiler_options,
        .working_directory = request.working_directory,
        .max_parallel_compiles = request.max_parallel_compiles,
    };

    const auto preparation_total = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - preparation_started);
    prepared.timings.compile_queue = preparation_total - prepared.timings.dependency_scan;
    return prepared;
}

} // namespace mqb::orchestration::detail
