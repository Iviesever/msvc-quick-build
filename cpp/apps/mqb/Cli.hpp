#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/core/BuildRequest.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"

namespace mqb::cli {

struct Options {
    BuildRequest build;
    std::optional<BuildConfiguration> configuration_override;
    std::optional<Architecture> architecture_override;
    std::optional<CppStandard> standard_override;
    std::vector<std::string> defines;
    std::vector<std::filesystem::path> include_directories;
    std::vector<std::filesystem::path> library_directories;
    std::vector<std::string> libraries;
    std::vector<std::string> compiler_arguments;
    std::vector<std::string> linker_arguments;
    msvc::ToolchainPreference toolchain_preference{msvc::ToolchainPreference::automatic};
    std::vector<std::filesystem::path> portable_roots;
    std::optional<bool> discovery_override;
    std::optional<std::size_t> jobs;
    bool discover_sources{true};
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
