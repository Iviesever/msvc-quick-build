#include "ProjectSetup.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mqb/msvc/MsvcParameterEngine.hpp"

namespace mqb::app {
namespace {

namespace fs = std::filesystem;

template <typename T>
[[nodiscard]] std::expected<void, std::string> merge_semantic_value(
    std::optional<T>& structured,
    const std::optional<T>& routed,
    const std::string_view layer,
    const std::string_view name) {
    if (!routed) {
        return {};
    }
    if (structured && *structured != *routed) {
        return std::unexpected(
            std::string{layer} + " has conflicting typed and native MSVC values for "
            + std::string{name});
    }
    structured = routed;
    return {};
}

[[nodiscard]] std::string parameter_error_message(
    const std::string_view layer,
    const mqb::msvc::ParameterError& error) {
    std::string message{layer};
    message += " ";
    message += mqb::msvc::to_string(error.tool);
    message += " parameter";
    if (!error.argument.empty()) {
        message += " '";
        message += error.argument;
        message += "'";
    }
    message += ": ";
    message += error.message;
    return message;
}

[[nodiscard]] std::expected<void, std::string> append_linker_file_input(
    std::vector<mqb::msvc::LinkerFileInput>& inputs,
    mqb::msvc::LinkerFileInput input,
    const std::string_view layer) {
    for (const auto& existing : inputs) {
        if (existing.kind == input.kind) {
            return std::unexpected(
                std::string{layer}
                + " introduces a second native MSVC /DEF module-definition file; "
                  "the effective LINK invocation may contain only one /DEF input");
        }
    }
    inputs.push_back(std::move(input));
    return {};
}

[[nodiscard]] std::expected<void, std::string> normalize_native_parameters(
    mqb::config::BuildOverrides& build,
    const std::string_view layer,
    const fs::path& path_base,
    std::vector<fs::path>& native_include_directories,
    std::vector<mqb::msvc::LinkerFileInput>& native_linker_file_inputs) {
    auto compiler = mqb::msvc::MsvcParameterEngine::route_compiler(
        std::span<const std::string>{build.compiler_arguments},
        path_base);
    if (!compiler) {
        return std::unexpected(parameter_error_message(layer, compiler.error()));
    }

    auto linker = mqb::msvc::MsvcParameterEngine::route_linker(
        std::span<const std::string>{build.linker_arguments});
    if (!linker) {
        return std::unexpected(parameter_error_message(layer, linker.error()));
    }
    auto linker_files = mqb::msvc::MsvcParameterEngine::linker_file_inputs(
        std::span<const std::string>{linker->passthrough},
        path_base);
    if (!linker_files) {
        return std::unexpected(parameter_error_message(layer, linker_files.error()));
    }
    for (auto& input : linker_files->inputs) {
        if (auto appended = append_linker_file_input(
                native_linker_file_inputs,
                std::move(input),
                layer); !appended) {
            return std::unexpected(appended.error());
        }
    }

    native_include_directories.insert(
        native_include_directories.end(),
        compiler->include_directories.begin(),
        compiler->include_directories.end());

    if (auto merged = merge_semantic_value(
            build.standard, compiler->standard, layer, "C++ standard"); !merged) {
        return std::unexpected(merged.error());
    }
    if (auto merged = merge_semantic_value(
            build.runtime_library, compiler->runtime_library, layer, "runtime library"); !merged) {
        return std::unexpected(merged.error());
    }
    if (auto merged = merge_semantic_value(
            build.architecture, linker->architecture, layer, "target architecture"); !merged) {
        return std::unexpected(merged.error());
    }
    if (auto merged = merge_semantic_value(
            build.subsystem, linker->subsystem, layer, "link subsystem"); !merged) {
        return std::unexpected(merged.error());
    }

    std::optional<bool> routed_ltcg = compiler->link_time_code_generation;
    if (linker->link_time_code_generation) {
        if (routed_ltcg && *routed_ltcg != *linker->link_time_code_generation) {
            return std::unexpected(
                std::string{layer}
                + " has conflicting native MSVC /GL and /LTCG policy");
        }
        routed_ltcg = linker->link_time_code_generation;
    }
    if (auto merged = merge_semantic_value(
            build.link_time_code_generation, routed_ltcg, layer, "LTCG"); !merged) {
        return std::unexpected(merged.error());
    }

    build.compiler_arguments = std::move(compiler->passthrough);
    build.linker_arguments = std::move(linker_files->passthrough);
    return {};
}

[[nodiscard]] std::string unknown_profile_message(
    const std::string_view requested,
    const mqb::config::ProjectConfig& config) {
    std::string message = "unknown profile '" + std::string{requested} + "'";
    if (config.profiles.empty()) {
        message += "; mqb.json defines no profiles";
        return message;
    }
    message += "; available profiles:";
    for (const auto& [name, profile] : config.profiles) {
        (void)profile;
        message += " '";
        message += name;
        message += "'";
    }
    return message;
}

} // namespace

std::expected<ProjectSetup, ProjectSetupError>
prepare_project(
    mqb::cli::Options& options,
    const std::filesystem::path& invocation_directory) {
    std::optional<mqb::config::ProjectConfig> project_config;
    auto located_config = mqb::config::ProjectConfigLoader::find_upwards(invocation_directory);
    if (!located_config) {
        return std::unexpected(ProjectSetupError{
            .message = {},
            .config_error = located_config.error(),
        });
    }
    if (located_config->has_value()) {
        auto loaded = mqb::config::ProjectConfigLoader::load(**located_config);
        if (!loaded) {
            return std::unexpected(ProjectSetupError{
                .message = {},
                .config_error = loaded.error(),
            });
        }
        project_config = std::move(*loaded);
    }

    const std::filesystem::path project_root = project_config
        ? project_config->project_root
        : invocation_directory;

    mqb::config::ProjectProfile* selected_profile = nullptr;
    if (options.profile) {
        if (!project_config) {
            return std::unexpected(ProjectSetupError{
                .message = "--profile requires an mqb.json project configuration",
                .config_error = std::nullopt,
            });
        }
        const auto profile = project_config->profiles.find(*options.profile);
        if (profile == project_config->profiles.end()) {
            return std::unexpected(ProjectSetupError{
                .message = unknown_profile_message(*options.profile, *project_config),
                .config_error = std::nullopt,
            });
        }
        selected_profile = &profile->second;
    }

    for (auto& provider : options.external_module_providers) {
        if (provider.interface_file.is_relative()) {
            provider.interface_file = (
                invocation_directory / provider.interface_file).lexically_normal();
        } else {
            provider.interface_file = provider.interface_file.lexically_normal();
        }
    }
    if (options.pch_override && options.pch_override->enabled) {
        if (options.pch_override->header.is_relative()) {
            options.pch_override->header = (
                invocation_directory / options.pch_override->header).lexically_normal();
        } else {
            options.pch_override->header = options.pch_override->header.lexically_normal();
        }
    }

    mqb::config::ProjectOverrides cli_overrides;
    cli_overrides.build.configuration = options.configuration_override;
    cli_overrides.build.architecture = options.architecture_override;
    cli_overrides.build.standard = options.standard_override;
    cli_overrides.build.target_kind = options.target_kind_override;
    cli_overrides.build.runtime_library = options.runtime_override;
    cli_overrides.build.link_time_code_generation = options.ltcg_override;
    cli_overrides.build.subsystem = options.subsystem_override;
    cli_overrides.build.precompiled_header = options.pch_override;
    cli_overrides.build.output_name = options.build.output_name;
    cli_overrides.build.defines = options.defines;
    cli_overrides.build.include_directories = options.include_directories;
    cli_overrides.build.library_directories = options.library_directories;
    cli_overrides.build.libraries = options.libraries;
    cli_overrides.build.compiler_arguments = options.compiler_arguments;
    cli_overrides.build.linker_arguments = options.linker_arguments;
    cli_overrides.discovery.enabled = options.discovery_override;
    cli_overrides.modules.external_providers = options.external_module_providers;

    std::vector<fs::path> native_include_directories;
    std::vector<mqb::msvc::LinkerFileInput> native_linker_file_inputs;
    if (project_config) {
        auto normalized = normalize_native_parameters(
            project_config->build,
            "mqb.json",
            project_root,
            native_include_directories,
            native_linker_file_inputs);
        if (!normalized) {
            return std::unexpected(ProjectSetupError{
                .message = normalized.error(),
                .config_error = std::nullopt,
            });
        }
    }
    if (selected_profile) {
        const std::string layer = "profile '" + *options.profile + "'";
        auto normalized = normalize_native_parameters(
            selected_profile->build,
            layer,
            project_root,
            native_include_directories,
            native_linker_file_inputs);
        if (!normalized) {
            return std::unexpected(ProjectSetupError{
                .message = normalized.error(),
                .config_error = std::nullopt,
            });
        }
    }
    if (auto normalized = normalize_native_parameters(
            cli_overrides.build,
            "CLI",
            invocation_directory,
            native_include_directories,
            native_linker_file_inputs); !normalized) {
        return std::unexpected(ProjectSetupError{
            .message = normalized.error(),
            .config_error = std::nullopt,
        });
    }

    const bool subsystem_explicit = cli_overrides.build.subsystem.has_value()
        || (selected_profile && selected_profile->build.subsystem.has_value())
        || (project_config && project_config->build.subsystem.has_value());

    auto effective = mqb::config::resolve_project_options(
        project_config ? &*project_config : nullptr,
        selected_profile,
        cli_overrides);
    options.build.configuration = effective.configuration;
    options.build.architecture = effective.architecture;
    options.build.standard = effective.standard;
    options.build.target_kind = effective.target_kind;
    options.runtime_override = effective.runtime_library;
    options.ltcg_override = effective.link_time_code_generation;
    options.subsystem_override = effective.subsystem;
    options.build.output_name = effective.output_name;
    options.discover_sources = effective.discovery_enabled;
    options.defines = effective.defines;
    options.include_directories = effective.include_directories;
    options.discovery_include_directories = options.include_directories;
    options.discovery_include_directories.insert(
        options.discovery_include_directories.end(),
        native_include_directories.begin(),
        native_include_directories.end());
    options.library_directories = effective.library_directories;
    options.libraries = effective.libraries;
    options.compiler_arguments = effective.compiler_arguments;
    options.linker_arguments = effective.linker_arguments;
    options.external_module_providers = effective.external_module_providers;

    if (options.build.run_after_build
        && options.build.target_kind != mqb::TargetKind::executable) {
        return std::unexpected(ProjectSetupError{
            .message = "--run is only valid for executable targets; `mqb run` also requires an executable target",
            .config_error = std::nullopt,
        });
    }

    std::vector<fs::path> linker_file_inputs;
    linker_file_inputs.reserve(native_linker_file_inputs.size());
    for (const auto& input : native_linker_file_inputs) {
        linker_file_inputs.push_back(input.path);
    }

    return ProjectSetup{
        .config = std::move(project_config),
        .effective = std::move(effective),
        .project_root = project_root,
        .linker_file_inputs = std::move(linker_file_inputs),
        .subsystem_explicit = subsystem_explicit,
    };
}

} // namespace mqb::app
