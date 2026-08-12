#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mqb/core/BuildPlan.hpp"
#include "mqb/core/BuildPlanner.hpp"
#include "mqb/core/CompileCache.hpp"
#include "mqb/core/CompileCacheFile.hpp"
#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/TranslationUnit.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/process/Process.hpp"

namespace mqb::orchestration {

struct IncrementalCompileRequest {
    TranslationUnit unit;
    CompilerOptions options;
    std::filesystem::path cache_file;
    std::filesystem::path source_dependencies_file;
    std::optional<std::filesystem::path> working_directory;
    bool force_rebuild{false};
};

enum class IncrementalCompileWarningCode {
    cache_load_failed,
    cache_save_failed,
    file_snapshot_failed,
};

struct IncrementalCompileWarning {
    IncrementalCompileWarningCode code{IncrementalCompileWarningCode::file_snapshot_failed};
    std::filesystem::path path;
    std::string message;
};

enum class IncrementalCompileErrorCode {
    planning_failed,
    compile_failed,
};

struct IncrementalCompileError {
    IncrementalCompileErrorCode code{IncrementalCompileErrorCode::planning_failed};
    std::string message;
    std::optional<BuildPlannerError> planner_error;
    std::optional<msvc::CompileExecutorError> compile_error;
};

struct IncrementalCompileResult {
    CompileCacheValidation validation;
    BuildPlan plan;
    bool compiled{false};
    std::optional<process::ProcessResult> process;
    std::vector<IncrementalCompileWarning> warnings;
};

class MsvcIncrementalCompileCoordinator {
public:
    MsvcIncrementalCompileCoordinator(
        const msvc::MsvcToolchain& toolchain,
        msvc::MsvcCompileExecutor& executor)
        : toolchain_(toolchain), executor_(executor) {}

    [[nodiscard]] std::expected<IncrementalCompileResult, IncrementalCompileError>
    run(const IncrementalCompileRequest& request) const;

private:
    const msvc::MsvcToolchain& toolchain_;
    msvc::MsvcCompileExecutor& executor_;
};

} // namespace mqb::orchestration
