#include "CompilerArgumentBuilder.hpp"

#include <filesystem>
#include <string>
#include <vector>

#include "CompilerInvocationValidation.hpp"
#include "mqb/core/TranslationUnitClassifier.hpp"

namespace mqb::msvc::detail {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string path_to_utf8(const fs::path& path) {
    const auto bytes = path.generic_u8string();
    return std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

[[nodiscard]] std::string standard_argument(const CppStandard standard) {
    switch (standard) {
    case CppStandard::cpp14: return "/std:c++14";
    case CppStandard::cpp17: return "/std:c++17";
    case CppStandard::cpp20: return "/std:c++20";
    case CppStandard::cpp23: return "/std:c++23preview";
    case CppStandard::latest: return "/std:c++latest";
    }
    return "/std:c++23preview";
}

[[nodiscard]] std::string runtime_argument(const RuntimeLibrary runtime) {
    switch (runtime) {
    case RuntimeLibrary::md: return "/MD";
    case RuntimeLibrary::mdd: return "/MDd";
    case RuntimeLibrary::mt: return "/MT";
    case RuntimeLibrary::mtd: return "/MTd";
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

void append_common_compile_arguments(
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
    if (!c_translation_unit) arguments.push_back(standard_argument(options.standard));
    for (const auto& define : options.defines) arguments.push_back("/D" + define);
    for (const auto& include_directory : options.include_directories) {
        arguments.push_back("/I" + path_to_utf8(include_directory));
    }
    for (const auto& argument : options.additional_arguments) arguments.push_back(argument);
    if (options.runtime_library) arguments.push_back(runtime_argument(*options.runtime_library));
    if (options.link_time_code_generation) arguments.emplace_back("/GL");
}

[[nodiscard]] std::string header_name_argument(const HeaderUnitLookupMethod lookup_method) {
    return lookup_method == HeaderUnitLookupMethod::angle ? "/headerName:angle" : "/headerName:quote";
}

[[nodiscard]] std::string header_unit_argument(const HeaderUnitLookupMethod lookup_method) {
    return lookup_method == HeaderUnitLookupMethod::angle ? "/headerUnit:angle" : "/headerUnit:quote";
}

} // namespace

std::expected<std::vector<std::string>, CompilerError>
build_compile_arguments(const CompileInvocation& invocation) {
    auto validated = validate_compile_invocation(invocation);
    if (!validated) return std::unexpected(validated.error());

    const bool c_translation_unit = is_c_translation_unit_path(invocation.source);
    std::vector<std::string> arguments;
    arguments.reserve(
        23 + invocation.options.defines.size()
        + invocation.options.include_directories.size()
        + invocation.options.additional_arguments.size()
        + (invocation.module_references.size() * 2)
        + (invocation.header_unit_references.size() * 2));

    append_common_compile_arguments(arguments, invocation.options, c_translation_unit);
    if (c_translation_unit) arguments.emplace_back("/TC");
    if (invocation.kind == TranslationUnitKind::module_interface) {
        arguments.emplace_back("/interface");
        arguments.emplace_back("/TP");
    }
    for (const auto& reference : invocation.module_references) {
        arguments.emplace_back("/reference");
        arguments.push_back(reference.logical_name + "=" + path_to_utf8(reference.interface_file));
    }
    for (const auto& reference : invocation.header_unit_references) {
        arguments.push_back(header_unit_argument(reference.lookup_method));
        arguments.push_back(reference.header_name + "=" + path_to_utf8(reference.interface_file));
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
build_header_unit_arguments(const HeaderUnitCompileInvocation& invocation) {
    auto validated = validate_header_unit_compile_invocation(invocation);
    if (!validated) return std::unexpected(validated.error());

    std::vector<std::string> arguments;
    arguments.reserve(
        24 + invocation.options.defines.size()
        + invocation.options.include_directories.size()
        + invocation.options.additional_arguments.size());
    append_common_compile_arguments(arguments, invocation.options, false);
    arguments.emplace_back("/exportHeader");
    arguments.push_back(header_name_argument(invocation.lookup_method));
    arguments.push_back(invocation.header_name);
    arguments.emplace_back("/ifcOutput");
    arguments.push_back(path_to_utf8(invocation.interface_output));
    if (invocation.source_dependencies) {
        arguments.emplace_back("/sourceDependencies");
        arguments.push_back(path_to_utf8(*invocation.source_dependencies));
    }
    if (invocation.object) arguments.push_back("/Fo" + path_to_utf8(*invocation.object));
    return arguments;
}

} // namespace mqb::msvc::detail
