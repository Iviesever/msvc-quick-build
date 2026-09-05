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

namespace detail {
class TargetCompileWave;
}

struct IncrementalCompileRequest {
    TranslationUnit unit;
    CompilerOptions options;
    std::filesystem::path cache_file;
    std::filesystem::path source_dependencies_file;
    // Named-module sources carry the P1689 artifact that preceded this compile.
    // A successful compile can then seal scan-reuse evidence into its compile
    // cache. Ordinary and header-unit compile requests leave this empty.
    std::optional<std::filesystem::path> module_scan_output;
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

struct IncrementalCompileInspection {
    CompileCacheValidation validation;
    BuildPlan plan;
    std::vector<IncrementalCompileWarning> warnings;
};

struct IncrementalCompileResult : IncrementalCompileInspection {
    bool compiled{false};
    std::optional<process::ProcessResult> process;
};

class MsvcIncrementalCompileCoordinator {
public:
    MsvcIncrementalCompileCoordinator(
        const msvc::MsvcToolchain& toolchain,
        msvc::MsvcCompileExecutor& executor)
        : toolchain_(toolchain), executor_(executor) {}

    // Read cache/filesystem evidence and produce the exact incremental decision
    // without launching cl.exe or mutating cache/output state.
    [[nodiscard]] std::expected<IncrementalCompileInspection, IncrementalCompileError>
    inspect(const IncrementalCompileRequest& request) const;

    [[nodiscard]] std::expected<IncrementalCompileResult, IncrementalCompileError>
    run(const IncrementalCompileRequest& request) const;

private:
    friend class detail::TargetCompileWave;

    // Only run() and the invocation-owned target wave may consume a decision.
    // Public inspect() remains diagnostic data, not a reusable execution ticket.
    [[nodiscard]] std::expected<IncrementalCompileResult, IncrementalCompileError>
    execute_inspected(
        const IncrementalCompileRequest& request,
        IncrementalCompileInspection inspection) const;

    const msvc::MsvcToolchain& toolchain_;
    msvc::MsvcCompileExecutor& executor_;
};

} // namespace mqb::orchestration
