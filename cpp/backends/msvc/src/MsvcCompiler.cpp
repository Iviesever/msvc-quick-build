#include "mqb/msvc/MsvcCompiler.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "mqb/core/TranslationUnitClassifier.hpp"

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

[[nodiscard]] bool predates_cpp20(const CppStandard standard) noexcept {
    return standard == CppStandard::cpp14 || standard == CppStandard::cpp17;
}

[[nodiscard]] std::string standard_argument(const CppStandard standard) {
    switch (standard) {
    case CppStandard::cpp14:
        return "/std:c++14";
    case CppStandard::cpp17:
        return "/std:c++17";
    case CppStandard::cpp20:
        return "/std:c++20";
    case CppStandard::cpp23:
        return "/std:c++23preview";
    case CppStandard::latest:
        return "/std:c++latest";
    }
    return "/std:c++23preview";
}

[[nodiscard]] std::string runtime_argument(const RuntimeLibrary runtime) {
    switch (runtime) {
    case RuntimeLibrary::md:
        return "/MD";
    case RuntimeLibrary::mdd:
        return "/MDd";
    case RuntimeLibrary::mt:
        return "/MT";
    case RuntimeLibrary::mtd:
        return "/MTd";
    }
    return "/MD";
}

void append_configuration_arguments(
    std::vector<std::string>& arguments,
    const BuildConfiguration configuration) {
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

[[nodiscard]] std::expected<void, CompilerError> append_common_compile_arguments(
    std::vector<std::string>& arguments,
    const CompilerOptions& options,
    const bool c_translation_unit) {
    arguments.emplace_back("/nologo");
    arguments.emplace_back("/c");
    arguments.emplace_back("/utf-8");
    arguments.emplace_back("/W3");

    if (!c_translation_unit) {
        arguments.emplace_back("/EHsc");
        arguments.emplace_back("/permissive-");
        arguments.emplace_back("/Zc:__cplusplus");
        arguments.emplace_back("/Zc:preprocessor");
    }
    arguments.emplace_back("/diagnostics:column");

    append_configuration_arguments(arguments, options.configuration);
    if (!c_translation_unit) {
        arguments.push_back(standard_argument(options.standard));
    }

    for (const auto& define : options.defines) {
        if (define.empty()) {
            return std::unexpected(invalid_request("compiler define must not be empty"));
        }
        arguments.push_back("/D" + define);
    }

    for (const auto& include_directory : options.include_directories) {
        if (include_directory.empty()) {
            return std::unexpected(invalid_request("include directory must not be empty"));
        }
        arguments.push_back("/I" + path_to_utf8(include_directory));
    }

    for (const auto& argument : options.additional_arguments) {
        if (argument.empty()) {
            return std::unexpected(invalid_request("additional compiler argument must not be empty"));
        }
        arguments.push_back(argument);
    }

    if (options.runtime_library) {
        // Typed runtime policy has higher precedence than the raw escape hatch.
        // Emitting it last keeps the explicit CRT selection authoritative while
        // leaving the historical default preset byte-for-byte unchanged.
        arguments.push_back(runtime_argument(*options.runtime_library));
    }

    return {};
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

[[nodiscard]] std::string header_unit_key(const HeaderUnitReference& reference) {
    return std::string{
        reference.lookup_method == HeaderUnitLookupMethod::angle ? "angle:" : "quote:"
    } + reference.header_name;
}

[[nodiscard]] std::expected<void, CompilerError> validate_module_contract(
    const CompileInvocation& invocation) {
    const bool uses_module_contract = invocation.kind == TranslationUnitKind::module_interface
        || invocation.module_interface_output.has_value()
        || !invocation.module_references.empty()
        || !invocation.header_unit_references.empty();
    if (uses_module_contract && is_c_translation_unit_path(invocation.source)) {
        return std::unexpected(invalid_request(
            "C translation units cannot participate in a C++ module/header-unit compile contract"));
    }
    if (uses_module_contract && predates_cpp20(invocation.options.standard)) {
        return std::unexpected(invalid_request(
            "module and header-unit compilation requires C++20 or newer"));
    }

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

    std::unordered_set<std::string> header_names;
    header_names.reserve(invocation.header_unit_references.size());
    for (const auto& reference : invocation.header_unit_references) {
        if (reference.header_name.empty()) {
            return std::unexpected(invalid_request(
                "header-unit reference name must not be empty"));
        }
        if (reference.interface_file.empty()) {
            return std::unexpected(invalid_request(
                "header-unit reference IFC path must not be empty"));
        }
        const std::string key = header_unit_key(reference);
        if (!header_names.emplace(key).second) {
            return std::unexpected(invalid_request(
                "duplicate header-unit reference '" + reference.header_name + "'"));
        }
    }
    return {};
}

[[nodiscard]] std::string header_name_argument(const HeaderUnitLookupMethod lookup_method) {
    return lookup_method == HeaderUnitLookupMethod::angle
        ? "/headerName:angle"
        : "/headerName:quote";
}

[[nodiscard]] std::string header_unit_argument(const HeaderUnitLookupMethod lookup_method) {
    return lookup_method == HeaderUnitLookupMethod::angle
        ? "/headerUnit:angle"
        : "/headerUnit:quote";
}

[[nodiscard]] std::expected<process::ProcessResult, CompilerError> run_compiler(
    const MsvcToolchain& toolchain,
    process::ProcessRunner& runner,
    std::vector<std::string> arguments,
    const std::optional<fs::path>& working_directory) {
    process::ProcessSpec spec;
    spec.executable = toolchain.identity.compiler;
    spec.arguments = std::move(arguments);
    spec.working_directory = working_directory;
    spec.environment = toolchain.environment;
    spec.inherit_environment = true;
    spec.capture_stdout = true;
    spec.capture_stderr = true;

    auto result = runner.run(spec);
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

    const bool c_translation_unit = is_c_translation_unit_path(invocation.source);
    std::vector<std::string> arguments;
    arguments.reserve(
        22
        + invocation.options.defines.size()
        + invocation.options.include_directories.size()
        + invocation.options.additional_arguments.size()
        + (invocation.module_references.size() * 2)
        + (invocation.header_unit_references.size() * 2));

    auto common = append_common_compile_arguments(
        arguments,
        invocation.options,
        c_translation_unit);
    if (!common) return std::unexpected(common.error());

    if (c_translation_unit) {
        // Keep source-language ownership structural. A raw /TP supplied earlier
        // cannot silently turn a .c target into C++ without changing its path.
        arguments.emplace_back("/TC");
    }

    if (invocation.kind == TranslationUnitKind::module_interface) {
        arguments.emplace_back("/interface");
        arguments.emplace_back("/TP");
    }

    for (const auto& reference : invocation.module_references) {
        arguments.emplace_back("/reference");
        arguments.push_back(
            reference.logical_name + "=" + path_to_utf8(reference.interface_file));
    }

    for (const auto& reference : invocation.header_unit_references) {
        arguments.push_back(header_unit_argument(reference.lookup_method));
        arguments.push_back(
            reference.header_name + "=" + path_to_utf8(reference.interface_file));
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

std::expected<std::vector<std::string>, CompilerError>
MsvcCompiler::build_header_unit_arguments(const HeaderUnitCompileInvocation& invocation) {
    if (invocation.header_name.empty()) {
        return std::unexpected(invalid_request("header-unit name must not be empty"));
    }
    if (invocation.interface_output.empty()) {
        return std::unexpected(invalid_request("header-unit IFC output path must not be empty"));
    }
    if (invocation.object && invocation.object->empty()) {
        return std::unexpected(invalid_request("header-unit object output path must not be empty"));
    }
    if (invocation.source_dependencies && invocation.source_dependencies->empty()) {
        return std::unexpected(invalid_request("sourceDependencies output path is empty"));
    }
    if (predates_cpp20(invocation.options.standard)) {
        return std::unexpected(invalid_request(
            "header-unit compilation requires C++20 or newer"));
    }

    std::vector<std::string> arguments;
    arguments.reserve(
        23
        + invocation.options.defines.size()
        + invocation.options.include_directories.size()
        + invocation.options.additional_arguments.size());

    auto common = append_common_compile_arguments(arguments, invocation.options, false);
    if (!common) return std::unexpected(common.error());

    arguments.emplace_back("/exportHeader");
    arguments.push_back(header_name_argument(invocation.lookup_method));
    arguments.push_back(invocation.header_name);
    arguments.emplace_back("/ifcOutput");
    arguments.push_back(path_to_utf8(invocation.interface_output));
    if (invocation.source_dependencies) {
        arguments.emplace_back("/sourceDependencies");
        arguments.push_back(path_to_utf8(*invocation.source_dependencies));
    }
    if (invocation.object) {
        arguments.push_back("/Fo" + path_to_utf8(*invocation.object));
    }
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

    return run_compiler(toolchain_, runner_, std::move(*arguments), invocation.working_directory);
}

std::expected<process::ProcessResult, CompilerError>
MsvcCompiler::compile_header_unit(const HeaderUnitCompileInvocation& invocation) const {
    auto arguments = build_header_unit_arguments(invocation);
    if (!arguments) {
        return std::unexpected(arguments.error());
    }

    auto prepared_interface = prepare_parent_directory(invocation.interface_output, "header-unit interface");
    if (!prepared_interface) {
        return std::unexpected(prepared_interface.error());
    }
    if (invocation.source_dependencies) {
        auto prepared_dependencies = prepare_parent_directory(
            *invocation.source_dependencies,
            "sourceDependencies");
        if (!prepared_dependencies) {
            return std::unexpected(prepared_dependencies.error());
        }
    }
    if (invocation.object) {
        auto prepared_object = prepare_parent_directory(*invocation.object, "header-unit object");
        if (!prepared_object) {
            return std::unexpected(prepared_object.error());
        }
    }

    return run_compiler(toolchain_, runner_, std::move(*arguments), invocation.working_directory);
}

} // namespace mqb::msvc
