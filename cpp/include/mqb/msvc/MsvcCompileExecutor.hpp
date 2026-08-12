#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

#include "mqb/core/CompileCache.hpp"
#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/TranslationUnit.hpp"
#include "mqb/msvc/MsvcCompiler.hpp"
#include "mqb/msvc/MsvcSourceDependenciesReader.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/process/Process.hpp"

namespace mqb::msvc {

struct CompileExecutionRequest {
    TranslationUnit unit;
    CompilerOptions options;
    std::filesystem::path source_dependencies_file;
    std::optional<std::filesystem::path> working_directory;
};

struct CompileExecutionResult {
    process::ProcessResult process;
    CompileCacheEntry cache_entry;
};

enum class CompileExecutorErrorCode {
    invalid_request,
    compiler_failed,
    dependency_metadata_failed,
    output_missing,
};

struct CompileExecutorError {
    CompileExecutorErrorCode code{CompileExecutorErrorCode::invalid_request};
    std::string message;
    std::optional<CompilerError> compiler_error;
    std::optional<SourceDependenciesError> dependency_error;
};

class MsvcCompileExecutor {
public:
    MsvcCompileExecutor(const MsvcToolchain& toolchain, process::ProcessRunner& runner)
        : toolchain_(toolchain), runner_(runner) {}

    [[nodiscard]] std::expected<CompileExecutionResult, CompileExecutorError>
    execute(const CompileExecutionRequest& request) const;

private:
    const MsvcToolchain& toolchain_;
    process::ProcessRunner& runner_;
};

} // namespace mqb::msvc
