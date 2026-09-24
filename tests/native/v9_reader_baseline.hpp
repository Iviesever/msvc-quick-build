// GENERATED FROM PINNED PRODUCTION TEXT; DO NOT EDIT.
#pragma once
#include <chrono>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <istream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "mqb/process/Process.hpp"
namespace mqb_v9_oracle {
namespace fs = std::filesystem;
using mqb::process::EnvironmentVariable;
namespace detail {
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


}
constexpr std::string_view cache_magic = "MQB_TOOLCHAIN_CACHE_V9";
constexpr std::uintmax_t max_cache_size = 1024u * 1024u;
constexpr std::size_t max_cache_entries = 64u;
constexpr std::size_t max_cache_string = 256u * 1024u;
constexpr auto max_cache_age = std::chrono::minutes{30};

struct CacheRecord {
    std::string target_architecture;
    std::string host_architecture;
    int preference{};
    fs::path vc_tools_root;
    std::string binary_stamp;
    std::vector<EnvironmentVariable> environment;
    std::string ambient_path;
    std::string effective_path;
};


[[nodiscard]] fs::path stable_path(fs::path path) {
    path = path.lexically_normal();
    while (path.filename().empty() && path.has_parent_path()) {
        const fs::path parent = path.parent_path();
        if (parent.empty() || parent == path) break;
        path = parent;
    }
    return path;
}


[[nodiscard]] bool environment_name_equal(const std::string_view left, const std::string_view right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(left[index]))
            != std::tolower(static_cast<unsigned char>(right[index]))) return false;
    }
    return true;
}

[[nodiscard]] bool cacheable_environment_name(const std::string_view name) {
    constexpr std::string_view names[]{
        "INCLUDE", "LIB", "LIBPATH", "VCToolsInstallDir", "WindowsSdkDir",
        "WindowsSDKVersion", "UniversalCRTSdkDir", "UCRTVersion", "NETFXSDKDir",
    };
    for (const auto candidate : names) {
        if (environment_name_equal(name, candidate)) return true;
    }
    return false;
}


[[nodiscard]] bool write_quoted(std::ostream& stream, const std::string_view label, const std::string& value) {
    if (value.size() > max_cache_string) return false;
    stream << label << ' ' << std::quoted(value) << '\n';
    return static_cast<bool>(stream);
}

[[nodiscard]] bool read_quoted(std::istream& stream, const std::string_view expected_label, std::string& value) {
    std::string label;
    if (!(stream >> label >> std::quoted(value))) return false;
    return label == expected_label && value.size() <= max_cache_string;
}

[[nodiscard]] bool write_record(std::ostream& stream, const CacheRecord& record) {
    stream << cache_magic << '\n';
    if (!write_quoted(stream, "target", record.target_architecture)
        || !write_quoted(stream, "host", record.host_architecture)) return false;
    stream << "preference " << record.preference << '\n';
    if (!write_quoted(stream, "vc_tools_root", detail::path_to_utf8(record.vc_tools_root))
        || !write_quoted(stream, "binary_stamp", record.binary_stamp)) return false;
    if (record.environment.size() > max_cache_entries) return false;
    stream << "environment " << record.environment.size() << '\n';
    for (const auto& variable : record.environment) {
        if (!write_quoted(stream, "env_name", variable.name) || !write_quoted(stream, "env_value", variable.value)) return false;
    }
    return write_quoted(stream, "ambient_path", record.ambient_path)
        && write_quoted(stream, "effective_path", record.effective_path);
}

[[nodiscard]] std::optional<std::size_t> read_count(std::istream& stream, const std::string_view expected_label) {
    std::string label;
    std::size_t count{};
    if (!(stream >> label >> count) || label != expected_label || count > max_cache_entries) return std::nullopt;
    return count;
}

[[nodiscard]] std::optional<CacheRecord> read_record(std::istream& stream) {
    std::string magic;
    if (!std::getline(stream, magic) || magic != cache_magic) return std::nullopt;
    CacheRecord record;
    if (!read_quoted(stream, "target", record.target_architecture) || !read_quoted(stream, "host", record.host_architecture)) return std::nullopt;
    std::string preference_label;
    if (!(stream >> preference_label >> record.preference) || preference_label != "preference") return std::nullopt;
    std::string root;
    if (!read_quoted(stream, "vc_tools_root", root) || !read_quoted(stream, "binary_stamp", record.binary_stamp)) return std::nullopt;
    record.vc_tools_root = stable_path(detail::path_from_utf8(root));
    const auto environment_count = read_count(stream, "environment");
    if (!environment_count) return std::nullopt;
    for (std::size_t index = 0; index < *environment_count; ++index) {
        EnvironmentVariable variable;
        if (!read_quoted(stream, "env_name", variable.name)
            || !read_quoted(stream, "env_value", variable.value)
            || !cacheable_environment_name(variable.name)) return std::nullopt;
        record.environment.push_back(std::move(variable));
    }
    if (!read_quoted(stream, "ambient_path", record.ambient_path)
        || !read_quoted(stream, "effective_path", record.effective_path)
        || record.effective_path.empty()) return std::nullopt;
    stream >> std::ws;
    if (!stream.eof()) return std::nullopt;
    return record;
}


}
