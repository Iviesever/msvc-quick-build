#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "Cli.hpp"

namespace mqb::app {

struct Invocation {
    std::filesystem::path directory;
    std::vector<std::filesystem::path> requested_sources;
};

[[nodiscard]] std::expected<Invocation, std::string>
resolve_invocation(mqb::cli::Options& options);

[[nodiscard]] std::expected<std::filesystem::path, std::string>
resolve_default_entry(
    const std::filesystem::path& project_root,
    const std::optional<std::filesystem::path>& configured_entry);

} // namespace mqb::app
