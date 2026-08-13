#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/msvc/MsvcToolchainLocator.hpp"

namespace mqb::msvc::detail {

[[nodiscard]] ToolchainError toolchain_failure(
    ToolchainErrorCode code,
    std::string message,
    std::filesystem::path path = {});

[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path path_from_utf8(std::string_view value);
[[nodiscard]] std::string trim_ascii(std::string value);
[[nodiscard]] std::optional<std::string> environment_value(const char* name);
[[nodiscard]] std::optional<std::filesystem::path> environment_path(const char* name);
[[nodiscard]] std::string architecture_name(Architecture architecture);
[[nodiscard]] std::filesystem::path host_directory(Architecture architecture);
[[nodiscard]] std::optional<std::filesystem::path> latest_directory(
    const std::filesystem::path& root);
[[nodiscard]] StandardLibraryModuleSources discover_standard_library_modules(
    const std::filesystem::path& vc_tools_root);
[[nodiscard]] std::expected<std::string, ToolchainError> binary_stamp(
    const std::filesystem::path& file);
[[nodiscard]] std::string prepend_environment(
    const std::vector<std::filesystem::path>& prefixes,
    const char* inherited_name);

} // namespace mqb::msvc::detail
