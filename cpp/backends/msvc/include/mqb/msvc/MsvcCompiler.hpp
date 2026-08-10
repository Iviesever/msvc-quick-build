#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/TranslationUnit.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/process/Process.hpp"

namespace mqb::msvc {

struct ModuleReference {
    std::string logical_name;
    std::filesystem::path interface_file;
};

struct CompileInvocation {
    std::filesystem::path source;
    std::filesystem::path object;
    std::optional<std::filesystem::path> source_dependencies;
    TranslationUnitKind kind{TranslationUnitKind::source};
    std::optional<std::filesystem::path> module_interface_output;
    std::vector<ModuleReference> module_references;
    CompilerOptions options;
    std::optional<std::filesystem::path> working_directory;
};

enum class CompilerErrorCode {
    invalid_request,
    output_prepare_failed,
    process_failed,
    compilation_failed,
};

struct CompilerError {
    CompilerErrorCode code{CompilerErrorCode::invalid_request};
    std::string message;
    std::optional<process::ProcessError> process_error;
    std::optional<process::ProcessResult> process_result;
};

class MsvcCompiler {
public:
    MsvcCompiler(const MsvcToolchain& toolchain, process::ProcessRunner& runner)
        : toolchain_(toolchain), runner_(runner) {}

    [[nodiscard]] static std::expected<std::vector<std::string>, CompilerError>
    build_arguments(const CompileInvocation& invocation);

    [[nodiscard]] std::expected<process::ProcessResult, CompilerError>
    compile(const CompileInvocation& invocation) const;

private:
    const MsvcToolchain& toolchain_;
    process::ProcessRunner& runner_;
};

} // namespace mqb::msvc
