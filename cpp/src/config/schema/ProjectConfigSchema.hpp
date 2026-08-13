#pragma once

#include <expected>
#include <filesystem>

#include "mqb/config/ProjectConfig.hpp"
#include "mqb/json/Json.hpp"

namespace mqb::config::detail {

[[nodiscard]] std::expected<ProjectConfig, Error> decode_project_config(
    const std::filesystem::path& file,
    const json::Value& root);

} // namespace mqb::config::detail
