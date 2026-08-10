#include "mqb/msvc/MsvcCompiler.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mqb::msvc {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] CompilerError invalid_request(std::string message) {
    return CompilerError{
        .code = CompilerErrorCode::invalid_request,
        .message = std::move(message),
    };
}

[[nodiscard]] std::string path_to_utf8(const fs::path& path) {
    const auto bytes = path.generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()};
}

[[nodiscard]] std::string standard_argument(const CppStandard standard) {
    switch (standard) {
    case CppStandard::cpp20:
        return "/std:c++20";
    case CppStandard::cpp23:
        return "/std:c++23";
    case CppStandard::latest:
        return "/std:c++latest";
    }
    return "/std:c++23";
}

void append_configuration_arguments(
    std::vector<std::string>& arguments,
    const BuildConfiguration configuration) {
    switch (configuration) {
    case BuildConfiguration::debug:
        arguments.emplace_back("/Od");
        arguments.emplace_back("/MDd");
        arguments.emplace_back("/Zi");
        arguments.emplace_back("/D_DEBUG");
        break;
    case BuildConfiguration::release:
        arguments.emplace_back("/O2");
        arguments.emplace_back("/Oi");
        arguments.emplace_back("/MD");
        arguments.emplace_back("/Zi");
        arguments.emplace_back("/DNDEBUG");
        break;
    }
}

} // namespace

std::expected<std::vector<std::string>, CompilerError>
MsvcCompiler::build_arguments(const CompileInvocation& invocation) {
    if (invocation.source.empty()) {
        return std::unexpected(invalid_request("compile source path is empty"));
    }
    if (invocation.object.empty()) {
        return std::unexpected(invalid_request("compile object path is empty"));
    }
    if (invocation.source_dependencies && invocation.source_dependencies->empty()) {
        return std::unexpected(invalid_request("sourceDependencies output path is empty"));
    }

    std::vector<std::string> arguments;
    arguments.reserve(
        16
        + invocation.options.defines.size()
        + invocation.options.include_directories.size()
        + invocation.options.additional_arguments.size());

    arguments.emplace_back("/nologo");
    arguments.emplace_back("/c");
    arguments.emplace_back("/utf-8");
    arguments.emplace_back("/W3");
    arguments.emplace_back("/EHsc");
    arguments.emplace_back("/permissive-");
    arguments.emplace_back("/Zc:__cplusplus");
    arguments.emplace_back("/Zc:preprocessor");
    arguments.emplace_back("/diagnostics:column");

    append_configuration_arguments(arguments, invocation.options.configuration);
    arguments.push_back(standard_argument(invocation.options.standard));

    for (const auto& define : invocation.options.defines) {
        if (define.empty()) {
            return std::unexpected(invalid_request("compiler define must not be empty"));
        }
        arguments.push_back("/D" + define);
    }

    for (const auto& include_directory : invocation.options.include_directories) {
        if (include_directory.empty()) {
            return std::unexpected(invalid_request("include directory must not be empty"));
        }
        arguments.push_back("/I" + path_to_utf8(include_directory));
    }

    for (const auto& argument : invocation.options.additional_arguments) {
        if (argument.empty()) {
            return std::unexpected(invalid_request("additional compiler argument must not be empty"));
        }
        arguments.push_back(argument);
    }

    if (invocation.source_dependencies) {
        arguments.emplace_back("/sourceDependencies");
        arguments.push_back(path_to_utf8(*invocation.source_dependencies));
    }

    // Structured artifact routing is appended after raw additional arguments so
    // a raw /Fo cannot silently redirect the object away from the BuildPlan.
    arguments.push_back("/Fo" + path_to_utf8(invocation.object));
    arguments.push_back(path_to_utf8(invocation.source));
    return arguments;
}

std::expected<process::ProcessResult, CompilerError>
MsvcCompiler::compile(const CompileInvocation& invocation) const {
    auto arguments = build_arguments(invocation);
    if (!arguments) {
        return std::unexpected(arguments.error());
    }

    process::ProcessSpec spec;
    spec.executable = toolchain_.identity.compiler;
    spec.arguments = std::move(*arguments);
    spec.working_directory = invocation.working_directory;
    spec.environment = toolchain_.environment;
    spec.inherit_environment = true;
    spec.capture_stdout = true;
    spec.capture_stderr = true;

    auto result = runner_.run(spec);
    if (!result) {
        return std::unexpected(CompilerError{
            .code = CompilerErrorCode::process_failed,
            .message = "failed to launch MSVC compiler",
            .process_error = result.error(),
        });
    }

    if (result->exit_code != 0) {
        return std::unexpected(CompilerError{
            .code = CompilerErrorCode::compilation_failed,
            .message = "MSVC compiler returned a non-zero exit code",
            .process_result = std::move(*result),
        });
    }

    return std::move(*result);
}

} // namespace mqb::msvc
