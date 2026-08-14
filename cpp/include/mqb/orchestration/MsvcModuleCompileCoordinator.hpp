#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

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
    ParallelismPolicy compile_parallelism{};
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

class MsvcModuleCompileCoordinator {
public:
    explicit MsvcModuleCompileCoordinator(
        MsvcIncrementalCompileCoordinator& compile_coordinator)
        : compile_coordinator_(compile_coordinator) {}

    [[nodiscard]] std::expected<ModuleCompileWaveResult, ModuleCompileError>
    run(const ModuleCompileWaveRequest& request) const;

private:
    MsvcIncrementalCompileCoordinator& compile_coordinator_;
};

} // namespace mqb::orchestration
