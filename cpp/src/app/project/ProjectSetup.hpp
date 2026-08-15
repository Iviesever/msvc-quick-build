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
    // Layer-normalized linker file evidence. The incremental linker currently
    // re-observes final argv directly; retaining this private app-layer record
    // keeps path provenance available without changing the public LinkOptions model.
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
