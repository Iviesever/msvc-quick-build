#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "Cli.hpp"
#include "mqb/config/ProjectConfig.hpp"
#include "mqb/config/ProjectOptions.hpp"

namespace mqb::app {

struct ProjectSetup {
    std::optional<mqb::config::ProjectConfig> config;
    mqb::config::EffectiveProjectOptions effective;
    std::filesystem::path project_root;
    // Non-owning file inputs extracted from validated native linker arguments
    // after each config/profile/CLI layer has resolved its own path base.
    std::vector<std::filesystem::path> linker_file_inputs;
    bool subsystem_explicit{false};
};

struct ProjectSetupError {
    std::string message;
    std::optional<mqb::config::Error> config_error;
};

[[nodiscard]] std::expected<ProjectSetup, ProjectSetupError>
prepare_project(
    mqb::cli::Options& options,
    const std::filesystem::path& invocation_directory);

} // namespace mqb::app
