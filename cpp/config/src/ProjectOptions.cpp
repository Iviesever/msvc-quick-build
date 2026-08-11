#include "mqb/config/ProjectOptions.hpp"

#include <utility>

namespace mqb::config {
namespace {

template <typename T>
void append_all(std::vector<T>& destination, const std::vector<T>& source) {
    destination.insert(destination.end(), source.begin(), source.end());
}

void apply_build(
    EffectiveProjectOptions& effective,
    const BuildOverrides& overrides) {
    if (overrides.configuration) effective.configuration = *overrides.configuration;
    if (overrides.architecture) effective.architecture = *overrides.architecture;
    if (overrides.standard) effective.standard = *overrides.standard;
    if (overrides.output_name) effective.output_name = overrides.output_name;
    append_all(effective.defines, overrides.defines);
    append_all(effective.include_directories, overrides.include_directories);
    append_all(effective.library_directories, overrides.library_directories);
    append_all(effective.libraries, overrides.libraries);
    append_all(effective.compiler_arguments, overrides.compiler_arguments);
    append_all(effective.linker_arguments, overrides.linker_arguments);
}

void apply_discovery(
    EffectiveProjectOptions& effective,
    const DiscoveryOverrides& overrides) {
    if (overrides.enabled) effective.discovery_enabled = *overrides.enabled;
    append_all(effective.discovery_exclude_directories, overrides.exclude_directories);
    append_all(effective.discovery_extra_sources, overrides.extra_sources);
    append_all(effective.discovery_exclude_sources, overrides.exclude_sources);
}

} // namespace

EffectiveProjectOptions resolve_project_options(
    const ProjectConfig* project_config,
    const ProjectOverrides& cli_overrides) {
    EffectiveProjectOptions effective;
    if (project_config != nullptr) {
        apply_build(effective, project_config->build);
        apply_discovery(effective, project_config->discovery);
    }
    apply_build(effective, cli_overrides.build);
    apply_discovery(effective, cli_overrides.discovery);
    return effective;
}

} // namespace mqb::config
