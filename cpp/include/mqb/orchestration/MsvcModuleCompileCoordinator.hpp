#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mqb/core/BuildArtifactRecord.hpp"
#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/core/TranslationUnit.hpp"
#include "mqb/modules/ModuleDependencyGraph.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/ParallelismPolicy.hpp"

namespace mqb::orchestration {

struct ModuleCompileSourceRequest {
    std::filesystem::path source;
    SourceArtifacts artifacts;
    TranslationUnitKind kind{TranslationUnitKind::source};
};

struct ModuleCompileHeaderUnitRequest {
    std::filesystem::path source;
    std::string header_name;
    HeaderUnitLookupMethod lookup_method{HeaderUnitLookupMethod::quote};
    SourceArtifacts artifacts;
};

struct ModuleCompileWaveRequest {
    std::vector<ModuleCompileSourceRequest> sources;
    std::vector<ModuleCompileHeaderUnitRequest> header_units;
    modules::ModuleDependencyPlan plan;
    CompilerOptions compiler_options;
    std::filesystem::path working_directory;
    ParallelismPolicy max_parallel_compiles{};
};

struct ModuleCompileInspection {
    std::filesystem::path source;
    // This is the exact typed request that real execution would pass to the
    // incremental compile coordinator after graph references and downstream
    // invalidation have been projected for this node.
    IncrementalCompileRequest request;
    IncrementalCompileInspection result;
};

struct HeaderUnitCompileInspection {
    std::filesystem::path source;
    std::string header_name;
    HeaderUnitLookupMethod lookup_method{HeaderUnitLookupMethod::quote};
    IncrementalCompileRequest request;
    IncrementalCompileInspection result;
};

struct ModuleCompileWaveInspection {
    // Results preserve request order; graph level and worker completion order
    // never leak into the public introspection surface.
    std::vector<ModuleCompileInspection> compiles;
    std::vector<HeaderUnitCompileInspection> header_unit_compiles;
    bool any_planned{false};
};

struct ModuleCompileResult {
    std::filesystem::path source;
    IncrementalCompileResult result;
};

struct HeaderUnitCompileResult {
    std::filesystem::path source;
    std::string header_name;
    HeaderUnitLookupMethod lookup_method{HeaderUnitLookupMethod::quote};
    IncrementalCompileResult result;
};

enum class ModuleCompileErrorCode {
    no_sources,
    invalid_parallelism,
    duplicate_source,
    invalid_artifact,
    artifact_collision,
    plan_source_missing,
    plan_source_duplicate,
    plan_source_unlisted,
    unresolved_requirement,
    invalid_provider,
    duplicate_reference,
    invalid_header_unit,
    scheduling_failed,
    inspection_failed,
    compile_failed,
};

struct ModuleCompileError {
    ModuleCompileErrorCode code{ModuleCompileErrorCode::no_sources};
    std::string message;
    std::filesystem::path source;
    std::filesystem::path artifact;
    std::filesystem::path provider_source;
    std::string logical_name;
    std::optional<IncrementalCompileError> compile_error;
};

struct ModuleCompileWaveResult {
    // Ordinary/module-TU results preserve request.sources order. Header-unit
    // results preserve request.header_units order; graph level and worker
    // completion order never leak into public result ordering.
    std::vector<ModuleCompileResult> compiles;
    std::vector<HeaderUnitCompileResult> header_unit_compiles;
    bool any_compiled{false};
};

// The caller's typed provider selection, accepted by the existing wave plan
// validation, and the actual per-node compile projections. This does not add
// a scan, infer missing provider names, or certify that the graph is current.
struct ModuleCompileWaveArtifactRecord {
    std::optional<ArtifactGenerationLabel> caller_label;
    modules::ModuleDependencyPlan dependencies;
    // Request order, not dependency-level or worker completion order.
    std::vector<ModuleCompileArtifactRecord> compiles;
    std::vector<ModuleCompileArtifactRecord> header_unit_compiles;

    static constexpr bool complete_producer_inventory = false;
    static constexpr bool deletion_authorized = false;
};

struct RecordedModuleCompileWaveResult {
    ModuleCompileWaveResult result;
    ModuleCompileWaveArtifactRecord record;
};

class MsvcModuleCompileCoordinator {
public:
    explicit MsvcModuleCompileCoordinator(
        MsvcIncrementalCompileCoordinator& compile_coordinator)
        : compile_coordinator_(compile_coordinator) {}

    // Resolve graph-level provider propagation and inspect every resulting
    // compile request without launching cl.exe or mutating output/cache state.
    [[nodiscard]] std::expected<ModuleCompileWaveInspection, ModuleCompileError>
    inspect(const ModuleCompileWaveRequest& request) const;

    [[nodiscard]] std::expected<ModuleCompileWaveResult, ModuleCompileError>
    run(const ModuleCompileWaveRequest& request) const;

    // Same invocation only. A failed wave returns the original error, not a
    // partial success record; earlier nodes may still have written files.
    [[nodiscard]] std::expected<RecordedModuleCompileWaveResult, ModuleCompileError>
    run_recorded(const ModuleCompileWaveRequest& request,
                 std::optional<ArtifactGenerationLabel> caller_label = std::nullopt) const;

private:
    [[nodiscard]] std::expected<ModuleCompileWaveResult, ModuleCompileError>
    run_impl(const ModuleCompileWaveRequest& request,
             ModuleCompileWaveArtifactRecord* record) const;

    MsvcIncrementalCompileCoordinator& compile_coordinator_;
};

} // namespace mqb::orchestration
