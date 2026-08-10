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

namespace mqb::orchestration {

struct ModuleCompileSourceRequest {
    std::filesystem::path source;
    SourceArtifacts artifacts;
    TranslationUnitKind kind{TranslationUnitKind::source};
};

struct ModuleCompileWaveRequest {
    std::vector<ModuleCompileSourceRequest> sources;
    modules::ModuleDependencyPlan plan;
    CompilerOptions compiler_options;
    std::filesystem::path working_directory;
    std::size_t max_parallel_compiles{1};
};

struct ModuleCompileResult {
    std::filesystem::path source;
    IncrementalCompileResult result;
};

enum class ModuleCompileErrorCode {
    no_sources,
    invalid_parallelism,
    duplicate_source,
    plan_source_missing,
    plan_source_duplicate,
    plan_source_unlisted,
    unresolved_requirement,
    invalid_provider,
    duplicate_reference,
    scheduling_failed,
    compile_failed,
};

struct ModuleCompileError {
    ModuleCompileErrorCode code{ModuleCompileErrorCode::no_sources};
    std::string message;
    std::filesystem::path source;
    std::filesystem::path provider_source;
    std::string logical_name;
    std::optional<IncrementalCompileError> compile_error;
};

struct ModuleCompileWaveResult {
    // Results are returned in request.sources order, independent of graph level
    // order and worker completion order.
    std::vector<ModuleCompileResult> compiles;
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
