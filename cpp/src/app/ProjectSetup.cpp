#include "ProjectSetup.hpp"

#include <optional>
#include <utility>

namespace mqb::app {

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

    const bool subsystem_explicit = options.subsystem_override.has_value()
        || (project_config && project_config->build.subsystem.has_value());
    const std::filesystem::path project_root = project_config
        ? project_config->project_root
        : invocation_directory;

    for (auto& provider : options.external_module_providers) {
        if (provider.interface_file.is_relative()) {
            provider.interface_file = (
                invocation_directory / provider.interface_file).lexically_normal();
        } else {
            provider.interface_file = provider.interface_file.lexically_normal();
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
    cli_overrides.build.output_name = options.build.output_name;
    cli_overrides.build.defines = options.defines;
    cli_overrides.build.include_directories = options.include_directories;
    cli_overrides.build.library_directories = options.library_directories;
    cli_overrides.build.libraries = options.libraries;
    cli_overrides.build.compiler_arguments = options.compiler_arguments;
    cli_overrides.build.linker_arguments = options.linker_arguments;
    cli_overrides.discovery.enabled = options.discovery_override;
    cli_overrides.modules.external_providers = options.external_module_providers;

    auto effective = mqb::config::resolve_project_options(
        project_config ? &*project_config : nullptr,
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
    options.library_directories = effective.library_directories;
    options.libraries = effective.libraries;
    options.compiler_arguments = effective.compiler_arguments;
    options.linker_arguments = effective.linker_arguments;
    options.external_module_providers = effective.external_module_providers;

    if (options.build.run_after_build
        && options.build.target_kind != mqb::TargetKind::executable) {
        return std::unexpected(ProjectSetupError{
            .message = "--run is only valid for executable targets",
            .config_error = std::nullopt,
        });
    }

    return ProjectSetup{
        .config = std::move(project_config),
        .effective = std::move(effective),
        .project_root = project_root,
        .subsystem_explicit = subsystem_explicit,
    };
}

} // namespace mqb::app
