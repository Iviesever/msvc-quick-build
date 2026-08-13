#include "CompilerExecution.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace mqb::msvc::detail {
namespace {
namespace fs = std::filesystem;

[[nodiscard]] std::expected<void, CompilerError> prepare_parent_directory(
    const fs::path& output,
    const std::string_view description) {
    const auto parent = output.parent_path();
    if (parent.empty()) return {};
    std::error_code error_code;
    fs::create_directories(parent, error_code);
    if (!error_code) return {};
    return std::unexpected(CompilerError{
        .code = CompilerErrorCode::output_prepare_failed,
        .message = "failed to prepare " + std::string{description} + " output directory",
    });
}

[[nodiscard]] std::expected<process::ProcessResult, CompilerError> run_compiler(
    const MsvcToolchain& toolchain,
    process::ProcessRunner& runner,
    std::vector<std::string> arguments,
    const std::optional<fs::path>& working_directory) {
    process::ProcessSpec spec{
        .executable = toolchain.identity.compiler,
        .arguments = std::move(arguments),
        .working_directory = working_directory,
        .environment = toolchain.environment,
        .inherit_environment = true,
        .capture_stdout = true,
        .capture_stderr = true,
    };
    auto result = runner.run(spec);
    if (!result) {
        return std::unexpected(CompilerError{
            .code = CompilerErrorCode::process_failed,
            .message = "failed to launch MSVC compiler",
            .process_error = result.error(),
        });
    }
    if (result->exit_code == 0) return std::move(*result);
    return std::unexpected(CompilerError{
        .code = CompilerErrorCode::compilation_failed,
        .message = "MSVC compiler returned a non-zero exit code",
        .process_result = std::move(*result),
    });
}

} // namespace

std::expected<process::ProcessResult, CompilerError>
execute_compile(
    const MsvcToolchain& toolchain,
    process::ProcessRunner& runner,
    const CompileInvocation& invocation,
    std::vector<std::string> arguments) {
    auto prepared = prepare_parent_directory(invocation.object, "object");
    if (!prepared) return std::unexpected(prepared.error());
    if (invocation.source_dependencies) {
        prepared = prepare_parent_directory(*invocation.source_dependencies, "sourceDependencies");
        if (!prepared) return std::unexpected(prepared.error());
    }
    if (invocation.module_interface_output) {
        prepared = prepare_parent_directory(*invocation.module_interface_output, "module interface");
        if (!prepared) return std::unexpected(prepared.error());
    }
    return run_compiler(toolchain, runner, std::move(arguments), invocation.working_directory);
}

std::expected<process::ProcessResult, CompilerError>
execute_header_unit_compile(
    const MsvcToolchain& toolchain,
    process::ProcessRunner& runner,
    const HeaderUnitCompileInvocation& invocation,
    std::vector<std::string> arguments) {
    auto prepared = prepare_parent_directory(invocation.interface_output, "header-unit interface");
    if (!prepared) return std::unexpected(prepared.error());
    if (invocation.source_dependencies) {
        prepared = prepare_parent_directory(*invocation.source_dependencies, "sourceDependencies");
        if (!prepared) return std::unexpected(prepared.error());
    }
    if (invocation.object) {
        prepared = prepare_parent_directory(*invocation.object, "header-unit object");
        if (!prepared) return std::unexpected(prepared.error());
    }
    return run_compiler(toolchain, runner, std::move(arguments), invocation.working_directory);
}

} // namespace mqb::msvc::detail
