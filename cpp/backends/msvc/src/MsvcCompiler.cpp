#include "mqb/msvc/MsvcCompiler.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
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
        return "/std:c++23preview";
    case CppStandard::latest:
        return "/std:c++latest";
    }
    return "/std:c++23preview";
}

void append_configuration_arguments(
    std::vector<std::string>& arguments,
    const BuildConfiguration configuration) {
    // /Z7 keeps compiler debug information inside each object instead of
    // writing a shared compiler PDB. This makes each TU artifact self-contained
    // and removes cross-process PDB write contention during parallel builds.
    switch (configuration) {
    case BuildConfiguration::debug:
        arguments.emplace_back("/Od");
        arguments.emplace_back("/MDd");
        arguments.emplace_back("/Z7");
        arguments.emplace_back("/D_DEBUG");
        break;
    case BuildConfiguration::release:
        arguments.emplace_back("/O2");
        arguments.emplace_back("/Oi");
        arguments.emplace_back("/MD");
        arguments.emplace_back("/Z7");
        arguments.emplace_back("/DNDEBUG");
        break;
    }
}

[[nodiscard]] std::expected<void, CompilerError> prepare_parent_directory(
    const fs::path& output,
    const std::string_view description) {
    const fs::path parent = output.parent_path();
    if (parent.empty()) {
        return {};
    }

    std::error_code error_code;
    fs::create_directories(parent, error_code);
    if (error_code) {
        return std::unexpected(CompilerError{
            .code = CompilerErrorCode::output_prepare_failed,
            .message = "failed to prepare " + std::string{description} + " output directory",
        });
    }
    return {};
}

[[nodiscard]] std::expected<void, CompilerError> validate_module_contract(
    const CompileInvocation& invocation) {
    if (invocation.kind == TranslationUnitKind::module_interface) {
        if (!invocation.module_interface_output
            || invocation.module_interface_output->empty()) {
            return std::unexpected(invalid_request(
                "module interface compilation requires a non-empty IFC output path"));
        }
    } else if (invocation.module_interface_output) {
        return std::unexpected(invalid_request(
            "ordinary source compilation must not request an IFC output"));
    }

    std::unordered_set<std::string> logical_names;
    logical_names.reserve(invocation.module_references.size());
    for (const auto& reference : invocation.module_references) {
        if (reference.logical_name.empty()) {
            return std::unexpected(invalid_request(
                "module reference logical name must not be empty"));
        }
        if (reference.interface_file.empty()) {
            return std::unexpected(invalid_request(
                "module reference IFC path must not be empty"));
        }
        if (!logical_names.emplace(reference.logical_name).second) {
            return std::unexpected(invalid_request(
                "duplicate module reference logical name '" + reference.logical_name + "'"));
        }
    }
    return {};
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
    auto module_contract = validate_module_contract(invocation);
    if (!module_contract) {
        return std::unexpected(module_contract.error());
    }

    std::vector<std::string> arguments;
    arguments.reserve(
        20
        + invocation.options.defines.size()
        + invocation.options.include_directories.size()
        + invocation.options.additional_arguments.size()
        + (invocation.module_references.size() * 2));

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

    // Module inputs and structured outputs are emitted after raw additional
    // arguments so the BuildPlan remains authoritative over semantic routing.
    if (invocation.kind == TranslationUnitKind::module_interface) {
        arguments.emplace_back("/interface");
        arguments.emplace_back("/TP");
    }

    for (const auto& reference : invocation.module_references) {
        arguments.emplace_back("/reference");
        arguments.push_back(
            reference.logical_name + "=" + path_to_utf8(reference.interface_file));
    }

    if (invocation.module_interface_output) {
        arguments.emplace_back("/ifcOutput");
        arguments.push_back(path_to_utf8(*invocation.module_interface_output));
    }

    if (invocation.source_dependencies) {
        arguments.emplace_back("/sourceDependencies");
        arguments.push_back(path_to_utf8(*invocation.source_dependencies));
    }

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

    auto prepared_object = prepare_parent_directory(invocation.object, "object");
    if (!prepared_object) {
        return std::unexpected(prepared_object.error());
    }
    if (invocation.source_dependencies) {
        auto prepared_dependencies = prepare_parent_directory(
            *invocation.source_dependencies,
            "sourceDependencies");
        if (!prepared_dependencies) {
            return std::unexpected(prepared_dependencies.error());
        }
    }
    if (invocation.module_interface_output) {
        auto prepared_interface = prepare_parent_directory(
            *invocation.module_interface_output,
            "module interface");
        if (!prepared_interface) {
            return std::unexpected(prepared_interface.error());
        }
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
