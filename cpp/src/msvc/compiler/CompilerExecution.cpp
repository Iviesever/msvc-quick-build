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

void suppress_ambient_compiler_options(
    std::vector<process::EnvironmentVariable>& environment) {
    // cl.exe prepends CL and appends _CL_ to its explicit argv. Those hidden
    // arguments would bypass MQB's Parameter Engine, structured ownership, and
    // compile identity. Remove the variables entirely: an empty value is still
    // a present environment variable and is not a general substitute for absence.
    environment.push_back(process::EnvironmentVariable{"CL", {}, true});
    environment.push_back(process::EnvironmentVariable{"_CL_", {}, true});
}

[[nodiscard]] std::expected<void, CompilerError> prepare_recipe_outputs(
    const MsvcCompileRecipe& recipe) {
    if (const auto* invocation = std::get_if<CompileInvocation>(&recipe.invocation)) {
        auto prepared = prepare_parent_directory(invocation->object, "object");
        if (!prepared) return std::unexpected(prepared.error());
        if (invocation->source_dependencies) {
            prepared = prepare_parent_directory(*invocation->source_dependencies, "sourceDependencies");
            if (!prepared) return std::unexpected(prepared.error());
        }
        if (invocation->module_interface_output) {
            prepared = prepare_parent_directory(*invocation->module_interface_output, "module interface");
            if (!prepared) return std::unexpected(prepared.error());
        }
        return {};
    }

    const auto& invocation = std::get<HeaderUnitCompileInvocation>(recipe.invocation);
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
    return {};
}

} // namespace

process::ProcessSpec make_compiler_process_spec(
    const MsvcToolchain& toolchain,
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
    suppress_ambient_compiler_options(spec.environment);
    return spec;
}

std::expected<process::ProcessResult, CompilerError>
execute_compile_recipe(
    process::ProcessRunner& runner,
    const MsvcCompileRecipe& recipe) {
    auto prepared = prepare_recipe_outputs(recipe);
    if (!prepared) return std::unexpected(prepared.error());

    auto result = runner.run(recipe.process);
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

} // namespace mqb::msvc::detail
