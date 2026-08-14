#include "ProjectSetup.hpp"

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "mqb/msvc/MsvcParameterEngine.hpp"

namespace mqb::app {
namespace {

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

[[nodiscard]] std::expected<void, std::string> normalize_native_parameters(
    mqb::config::BuildOverrides& build,
    const std::string_view layer) {
    auto compiler = mqb::msvc::MsvcParameterEngine::route_compiler(
        std::span<const std::string>{build.compiler_arguments});
    if (!compiler) {
        return std::unexpected(parameter_error_message(layer, compiler.error()));
    }

    auto linker = mqb::msvc::MsvcParameterEngine::route_linker(
        std::span<const std::string>{build.linker_arguments});
    if (!linker) {
        return std::unexpected(parameter_error_message(layer, linker.error()));
    }

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
    build.linker_arguments = std::move(linker->passthrough);
    return {};
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

    if (project_config) {
        auto normalized = normalize_native_parameters(
            project_config->build,
            "mqb.json");
        if (!normalized) {
            return std::unexpected(ProjectSetupError{
                .message = normalized.error(),
                .config_error = std::nullopt,
            });
        }
    }
    if (auto normalized = normalize_native_parameters(
            cli_overrides.build,
            "CLI"); !normalized) {
        return std::unexpected(ProjectSetupError{
            .message = normalized.error(),
            .config_error = std::nullopt,
        });
    }

    const bool subsystem_explicit = cli_overrides.build.subsystem.has_value()
        || (project_config && project_config->build.subsystem.has_value());

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
