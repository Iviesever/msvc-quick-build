#include "mqb/msvc/MsvcModuleDependencyScanner.hpp"

#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

namespace mqb::msvc {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] ModuleScanError failure(
    const ModuleScanErrorCode code,
    std::string message) {
    return ModuleScanError{
        .code = code,
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

void append_configuration_macro(
    std::vector<std::string>& arguments,
    const BuildConfiguration configuration) {
    switch (configuration) {
    case BuildConfiguration::debug:
        arguments.emplace_back("/D_DEBUG");
        break;
    case BuildConfiguration::release:
        arguments.emplace_back("/DNDEBUG");
        break;
    }
}

[[nodiscard]] std::expected<void, ModuleScanError> prepare_output(
    const fs::path& output) {
    const fs::path parent = output.parent_path();
    if (!parent.empty()) {
        std::error_code create_error;
        fs::create_directories(parent, create_error);
        if (create_error) {
            return std::unexpected(failure(
                ModuleScanErrorCode::output_prepare_failed,
                "failed to prepare module dependency output directory"));
        }
    }

    std::error_code remove_error;
    fs::remove(output, remove_error);
    if (remove_error) {
        return std::unexpected(failure(
            ModuleScanErrorCode::stale_output_remove_failed,
            "failed to remove stale module dependency output"));
    }
    return {};
}

[[nodiscard]] std::expected<std::string, ModuleScanError> read_text_file(
    const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::unexpected(failure(
            ModuleScanErrorCode::output_read_failed,
            "failed to open module dependency output"));
    }

    std::string text{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
    if (input.bad()) {
        return std::unexpected(failure(
            ModuleScanErrorCode::output_read_failed,
            "failed while reading module dependency output"));
    }
    return text;
}

} // namespace

std::expected<std::vector<std::string>, ModuleScanError>
MsvcModuleDependencyScanner::build_arguments(const ModuleScanInvocation& invocation) {
    if (invocation.source.empty()) {
        return std::unexpected(failure(
            ModuleScanErrorCode::invalid_request,
            "module scan source path is empty"));
    }
    if (invocation.output_file.empty()) {
        return std::unexpected(failure(
            ModuleScanErrorCode::invalid_request,
            "module scan output path is empty"));
    }

    std::vector<std::string> arguments;
    arguments.reserve(
        12
        + invocation.options.defines.size()
        + invocation.options.include_directories.size()
        + invocation.options.additional_arguments.size());

    arguments.emplace_back("/nologo");
    arguments.emplace_back("/utf-8");
    arguments.emplace_back("/permissive-");
    arguments.emplace_back("/Zc:preprocessor");
    arguments.emplace_back("/diagnostics:column");
    append_configuration_macro(arguments, invocation.options.configuration);
    arguments.push_back(standard_argument(invocation.options.standard));

    for (const auto& define : invocation.options.defines) {
        if (define.empty()) {
            return std::unexpected(failure(
                ModuleScanErrorCode::invalid_request,
                "compiler define must not be empty"));
        }
        arguments.push_back("/D" + define);
    }

    for (const auto& include_directory : invocation.options.include_directories) {
        if (include_directory.empty()) {
            return std::unexpected(failure(
                ModuleScanErrorCode::invalid_request,
                "include directory must not be empty"));
        }
        arguments.push_back("/I" + path_to_utf8(include_directory));
    }

    for (const auto& argument : invocation.options.additional_arguments) {
        if (argument.empty()) {
            return std::unexpected(failure(
                ModuleScanErrorCode::invalid_request,
                "additional compiler argument must not be empty"));
        }
        arguments.push_back(argument);
    }

    if (invocation.kind == TranslationUnitKind::module_interface) {
        arguments.emplace_back("/interface");
        arguments.emplace_back("/TP");
    }

    // Keep structured scan routing after raw additional arguments so a caller
    // cannot silently redirect dependency metadata away from the BuildPlan.
    arguments.emplace_back("/scanDependencies");
    arguments.push_back(path_to_utf8(invocation.output_file));
    arguments.push_back(path_to_utf8(invocation.source));
    return arguments;
}

std::expected<ModuleScanResult, ModuleScanError>
MsvcModuleDependencyScanner::scan(const ModuleScanInvocation& invocation) const {
    auto arguments = build_arguments(invocation);
    if (!arguments) {
        return std::unexpected(arguments.error());
    }

    auto prepared = prepare_output(invocation.output_file);
    if (!prepared) {
        return std::unexpected(prepared.error());
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
        ModuleScanError error = failure(
            ModuleScanErrorCode::process_failed,
            "failed to launch MSVC module dependency scan");
        error.process_error = result.error();
        return std::unexpected(std::move(error));
    }

    if (result->exit_code != 0) {
        ModuleScanError error = failure(
            ModuleScanErrorCode::scan_failed,
            "MSVC module dependency scan returned a non-zero exit code");
        error.process_result = std::move(*result);
        return std::unexpected(std::move(error));
    }

    std::error_code exists_error;
    const bool exists = fs::is_regular_file(invocation.output_file, exists_error);
    if (exists_error || !exists) {
        ModuleScanError error = failure(
            ModuleScanErrorCode::output_missing,
            "MSVC module dependency scan did not produce the requested JSON file");
        error.process_result = std::move(*result);
        return std::unexpected(std::move(error));
    }

    auto text = read_text_file(invocation.output_file);
    if (!text) {
        auto error = text.error();
        error.process_result = std::move(*result);
        return std::unexpected(std::move(error));
    }

    auto dependencies = modules::P1689Parser::parse(*text);
    if (!dependencies) {
        ModuleScanError error = failure(
            ModuleScanErrorCode::dependency_metadata_failed,
            "failed to parse MSVC P1689 module dependency metadata");
        error.process_result = std::move(*result);
        error.dependency_error = dependencies.error();
        return std::unexpected(std::move(error));
    }

    return ModuleScanResult{
        .process = std::move(*result),
        .dependencies = std::move(*dependencies),
    };
}

} // namespace mqb::msvc
