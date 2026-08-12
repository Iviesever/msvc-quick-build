#pragma once

#include <expected>
#include <filesystem>
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

} // namespace mqb::app
