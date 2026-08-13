#include "ToolchainDiscoveryPrimitives.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <utility>
#include <vector>

namespace mqb::msvc::detail {

namespace fs = std::filesystem;
using process::EnvironmentVariable;

ToolchainError toolchain_failure(
    const ToolchainErrorCode code,
    std::string message,
    fs::path path) {
    return ToolchainError{
        .code = code,
        .path = std::move(path),
        .message = std::move(message),
    };
}

std::string path_to_utf8(const fs::path& path) {
    const auto bytes = path.generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()};
}

fs::path path_from_utf8(const std::string_view value) {
    std::u8string bytes;
    bytes.assign(
        reinterpret_cast<const char8_t*>(value.data()),
        reinterpret_cast<const char8_t*>(value.data() + value.size()));
    return fs::path{bytes};
}

std::string trim_ascii(std::string value) {
    const auto is_space = [](const unsigned char ch) {
        return std::isspace(ch) != 0;
    };

    value.erase(
        value.begin(),
        std::find_if(value.begin(), value.end(), [&](const char ch) {
            return !is_space(static_cast<unsigned char>(ch));
        }));
    value.erase(
        std::find_if(value.rbegin(), value.rend(), [&](const char ch) {
            return !is_space(static_cast<unsigned char>(ch));
        }).base(),
        value.end());

    constexpr std::string_view utf8_bom{"\xef\xbb\xbf"};
    if (value.starts_with(utf8_bom)) {
        value.erase(0, utf8_bom.size());
    }
    return value;
}

std::optional<std::string> environment_value(const char* name) {
    if (const char* value = std::getenv(name); value != nullptr) {
        return std::string{value};
    }
    return std::nullopt;
}

std::optional<fs::path> environment_path(const char* name) {
    const auto value = environment_value(name);
    if (!value || value->empty()) {
        return std::nullopt;
    }
    return path_from_utf8(*value);
}

std::string architecture_name(const Architecture architecture) {
    return architecture == Architecture::x86 ? "x86" : "x64";
}

fs::path host_directory(const Architecture architecture) {
    return architecture == Architecture::x86 ? fs::path{"Hostx86"} : fs::path{"Hostx64"};
}

std::optional<fs::path> latest_directory(const fs::path& root) {
    std::error_code error_code;
    if (!fs::is_directory(root, error_code)) {
        return std::nullopt;
    }

    std::vector<fs::path> directories;
    for (fs::directory_iterator iterator{root, error_code};
         !error_code && iterator != fs::directory_iterator{};
         iterator.increment(error_code)) {
        if (iterator->is_directory(error_code) && !error_code) {
            directories.push_back(iterator->path());
        }
    }
    if (error_code || directories.empty()) {
        return std::nullopt;
    }

    std::ranges::sort(
        directories,
        {},
        [](const fs::path& path) { return path.filename().native(); });
    return directories.back();
}

StandardLibraryModuleSources discover_standard_library_modules(
    const fs::path& vc_tools_root) {
    StandardLibraryModuleSources sources;
    const fs::path modules = vc_tools_root / "modules";
    std::error_code error_code;

    const fs::path std_source = modules / "std.ixx";
    if (fs::is_regular_file(std_source, error_code) && !error_code) {
        sources.std = std_source.lexically_normal();
    }

    error_code.clear();
    const fs::path std_compat_source = modules / "std.compat.ixx";
    if (fs::is_regular_file(std_compat_source, error_code) && !error_code) {
        sources.std_compat = std_compat_source.lexically_normal();
    }
    return sources;
}

std::expected<std::string, ToolchainError> binary_stamp(const fs::path& file) {
    std::error_code error_code;
    const auto size = fs::file_size(file, error_code);
    if (error_code) {
        return std::unexpected(toolchain_failure(
            ToolchainErrorCode::compiler_not_found,
            "cannot read compiler size",
            file));
    }

    const auto modified = fs::last_write_time(file, error_code);
    if (error_code) {
        return std::unexpected(toolchain_failure(
            ToolchainErrorCode::compiler_not_found,
            "cannot read compiler timestamp",
            file));
    }

    return std::to_string(size) + ":" + std::to_string(modified.time_since_epoch().count());
}

std::string prepend_environment(
    const std::vector<fs::path>& prefixes,
    const char* inherited_name) {
    std::string value;
    for (const auto& prefix : prefixes) {
        if (!value.empty()) {
            value.push_back(';');
        }
        value += path_to_utf8(prefix);
    }

    if (const auto inherited = environment_value(inherited_name);
        inherited && !inherited->empty()) {
        if (!value.empty()) {
            value.push_back(';');
        }
        value += *inherited;
    }
    return value;
}

} // namespace mqb::msvc::detail
