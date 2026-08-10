#pragma once

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/core/BuildRequest.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"

namespace mqb::cli {

struct Options {
    BuildRequest build;
    std::vector<std::string> defines;
    std::vector<std::filesystem::path> include_directories;
    msvc::ToolchainPreference toolchain_preference{msvc::ToolchainPreference::automatic};
    std::vector<std::filesystem::path> portable_roots;
    bool verbose{false};
    bool show_help{false};
};

struct Error {
    std::string message;
};

[[nodiscard]] std::expected<Options, Error>
parse_arguments(std::span<const std::string_view> arguments);

[[nodiscard]] std::string_view usage() noexcept;

} // namespace mqb::cli
