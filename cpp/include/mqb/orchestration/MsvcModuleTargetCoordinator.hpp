#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/LinkOptions.hpp"
#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/modules/ModuleDependencyGraph.hpp"
#include "mqb/msvc/MsvcModuleDependencyScanner.hpp"
#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalModuleScanCoordinator.hpp"
#include "mqb/orchestration/MsvcModuleCompileCoordinator.hpp"
#include "mqb/orchestration/ParallelismPolicy.hpp"
#include "mqb/orchestration/TargetTimings.hpp"

namespace mqb::orchestration {

struct IncrementalModuleTargetRequest {
    std::vector<ModuleCompileSourceRequest> sources;
    TargetArtifacts target;
    // Header-unit and toolchain-owned standard-library module providers are
    // discovered only after P1689 scanning. The target coordinator uses this
    // layout to assign their project-local generated artifacts dynamically.
    std::optional<ProjectArtifactLayout> artifact_layout;
    CompilerOptions compiler_options;
    LinkOptions link_options;
    std::filesystem::path working_directory;
    ParallelismPolicy max_parallel_scans{};
    ParallelismPolicy max_parallel_compiles{};
};

struct ModuleTargetScanResult {
    std::filesystem::path source;
    msvc::ModuleScanResult result;
};

struct ModuleTargetScanInspection {
    std::filesystem::path source;
    IncrementalModuleScanInspection result;
    // Requested project sources are reported first in request order. Any
    // selected VC Tools std/std.compat provider is appended and marked here so
    // user-facing introspection can distinguish project and toolchain ownership.
    bool toolchain_owned{false};
};

struct IncrementalModuleTargetInspection {
    std::vector<ModuleTargetScanInspection> scans;
    // These stages remain absent whenever any required P1689 scan is stale or
    // missing. A dependency graph cannot be inferred honestly until every
    // required scan document is reusable.
    std::optional<modules::ModuleDependencyPlan> plan;
    std::optional<ModuleCompileWaveRequest> compile_request;
    std::optional<ModuleCompileWaveInspection> compiles;
    std::optional<IncrementalLinkRequest> link_request;
    std::optional<IncrementalLinkInspection> link;

    [[nodiscard]] bool graph_ready() const noexcept {
        return plan.has_value();
    }

    [[nodiscard]] bool scan_required() const noexcept {
        return !graph_ready();
    }
};

enum class IncrementalModuleTargetErrorCode {
    no_sources,
    invalid_parallelism,
    duplicate_source,
    invalid_artifact,
    artifact_collision,
    artifact_layout_missing,
    artifact_layout_failed,
    invalid_header_unit,
    standard_library_module_unavailable,
    standard_library_module_standard_unsupported,
    scheduling_failed,
    scan_failed,
    invalid_scan_result,
    graph_failed,
    compile_failed,
    link_failed,
};

struct IncrementalModuleTargetError {
    IncrementalModuleTargetErrorCode code{IncrementalModuleTargetErrorCode::no_sources};
    std::string message;
    std::filesystem::path source;
    std::filesystem::path artifact;
    std::optional<ArtifactLayoutError> artifact_layout_error;
    std::optional<msvc::ModuleScanError> scan_error;
    std::optional<modules::ModuleGraphError> graph_error;
    std::optional<ModuleCompileError> compile_error;
    std::optional<IncrementalLinkError> link_error;
};

struct IncrementalModuleTargetResult {
    // Public source scans preserve request.sources order. Compile waves may also
    // contain dynamically injected toolchain-owned module providers after the
    // original source prefix; routers can keep those implementation details out
    // of ordinary CLI compile-result ordering while still honoring any_compiled.
    std::vector<ModuleTargetScanResult> scans;
    modules::ModuleDependencyPlan plan;
    ModuleCompileWaveResult compiles;
    IncrementalLinkResult link;
    TargetTimings timings;
};

class MsvcModuleTargetCoordinator {
public:
    MsvcModuleTargetCoordinator(
        msvc::MsvcModuleDependencyScanner& scanner,
        MsvcModuleCompileCoordinator& compile_coordinator,
        MsvcIncrementalLinkCoordinator& link_coordinator)
        : scanner_(scanner),
          compile_coordinator_(compile_coordinator),
          link_coordinator_(link_coordinator) {}

    // Inspect the complete module target without launching cl.exe/link.exe or
    // mutating scan, compile, link, or output state. Cold/stale scan evidence
    // yields scan recipes only; warm trustworthy P1689 continues through graph,
    // compile-wave, and final-link inspection.
    [[nodiscard]] std::expected<IncrementalModuleTargetInspection, IncrementalModuleTargetError>
    inspect(const IncrementalModuleTargetRequest& request) const;

    [[nodiscard]] std::expected<IncrementalModuleTargetResult, IncrementalModuleTargetError>
    run(const IncrementalModuleTargetRequest& request) const;

private:
    msvc::MsvcModuleDependencyScanner& scanner_;
    MsvcModuleCompileCoordinator& compile_coordinator_;
    MsvcIncrementalLinkCoordinator& link_coordinator_;
};

} // namespace mqb::orchestration
