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

using HeaderUnitReference = mqb::HeaderUnitReference;
using ModuleReference = mqb::ModuleReference;

struct CompileInvocation {
    std::filesystem::path source;
    std::filesystem::path object;
    std::optional<std::filesystem::path> source_dependencies;
    TranslationUnitKind kind{TranslationUnitKind::source};
    std::optional<std::filesystem::path> module_interface_output;
    std::vector<ModuleReference> module_references;
    std::vector<HeaderUnitReference> header_unit_references;
    CompilerOptions options;
    std::optional<std::filesystem::path> working_directory;
};

struct HeaderUnitCompileInvocation {
    std::string header_name;
    HeaderUnitLookupMethod lookup_method{HeaderUnitLookupMethod::quote};
    std::filesystem::path interface_output;
    std::optional<std::filesystem::path> object;
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

    [[nodiscard]] static std::expected<std::vector<std::string>, CompilerError>
    build_header_unit_arguments(const HeaderUnitCompileInvocation& invocation);

    [[nodiscard]] std::expected<process::ProcessResult, CompilerError>
    compile(const CompileInvocation& invocation) const;

    [[nodiscard]] std::expected<process::ProcessResult, CompilerError>
    compile_header_unit(const HeaderUnitCompileInvocation& invocation) const;

private:
    const MsvcToolchain& toolchain_;
    process::ProcessRunner& runner_;
};

} // namespace mqb::msvc
