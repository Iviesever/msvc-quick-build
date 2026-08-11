#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mqb/config/ProjectConfig.hpp"

namespace mqb::config {

struct ProjectOverrides {
    BuildOverrides build;
    DiscoveryOverrides discovery;
};

struct EffectiveProjectOptions {
    BuildConfiguration configuration{BuildConfiguration::debug};
    Architecture architecture{Architecture::x64};
    CppStandard standard{CppStandard::cpp23};
    std::optional<std::string> output_name;
    std::vector<std::string> defines;
    std::vector<std::filesystem::path> include_directories;
    std::vector<std::filesystem::path> library_directories;
    std::vector<std::string> libraries;
    std::vector<std::string> compiler_arguments;
    std::vector<std::string> linker_arguments;

    bool discovery_enabled{true};
    std::vector<std::filesystem::path> discovery_exclude_directories;
    std::vector<std::filesystem::path> discovery_extra_sources;
    std::vector<std::filesystem::path> discovery_exclude_sources;
};

[[nodiscard]] EffectiveProjectOptions resolve_project_options(
    const ProjectConfig* project_config,
    const ProjectOverrides& cli_overrides);

} // namespace mqb::config
