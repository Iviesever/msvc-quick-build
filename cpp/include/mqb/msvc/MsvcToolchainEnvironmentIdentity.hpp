#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/process/Process.hpp"

namespace mqb::msvc {
namespace toolchain_environment_detail {

[[nodiscard]] inline bool ascii_iequals(
    const std::string_view left,
    const std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto a = static_cast<unsigned char>(left[index]);
        const auto b = static_cast<unsigned char>(right[index]);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return true;
}

[[nodiscard]] inline const process::EnvironmentVariable* find_environment(
    const std::span<const process::EnvironmentVariable> environment,
    const std::string_view name) noexcept {
    // Process environment overlays are last-wins. Preserve that rule if a
    // synthetic/test toolchain happens to contain duplicate spellings.
    for (auto iterator = environment.rbegin(); iterator != environment.rend(); ++iterator) {
        if (ascii_iequals(iterator->name, name)) return &*iterator;
    }
    return nullptr;
}

inline void hash_byte(std::uint64_t& hash, const std::uint8_t byte) noexcept {
    constexpr std::uint64_t fnv_prime = 1099511628211ull;
    hash ^= byte;
    hash *= fnv_prime;
}

inline void hash_u64(std::uint64_t& hash, const std::uint64_t value) noexcept {
    for (std::uint32_t shift = 0; shift < 64u; shift += 8u) {
        hash_byte(hash, static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

inline void hash_string(std::uint64_t& hash, const std::string_view value) noexcept {
    hash_u64(hash, static_cast<std::uint64_t>(value.size()));
    for (const unsigned char byte : value) hash_byte(hash, byte);
}

[[nodiscard]] inline std::string hex64(std::uint64_t value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (std::size_t index = result.size(); index > 0; --index) {
        result[index - 1] = digits[value & 0x0fu];
        value >>= 4u;
    }
    return result;
}

template <std::size_t Count>
[[nodiscard]] inline std::string environment_stamp(
    const std::span<const process::EnvironmentVariable> environment,
    const std::array<std::string_view, Count>& names,
    const std::string_view domain) {
    constexpr std::uint64_t fnv_offset_basis = 14695981039346656037ull;
    std::uint64_t hash = fnv_offset_basis;
    hash_string(hash, domain);
    for (const auto name : names) {
        hash_string(hash, name);
        const auto* variable = find_environment(environment, name);
        hash_byte(hash, variable == nullptr ? 0u : 1u);
        if (variable != nullptr) hash_string(hash, variable->value);
    }
    return std::string{domain} + ":" + hex64(hash);
}

[[nodiscard]] inline std::filesystem::path path_from_utf8(const std::string_view value) {
    std::u8string bytes;
    bytes.assign(
        reinterpret_cast<const char8_t*>(value.data()),
        reinterpret_cast<const char8_t*>(value.data() + value.size()));
    return std::filesystem::path{bytes};
}

[[nodiscard]] inline std::filesystem::path stable_path(std::filesystem::path path) {
    path = path.lexically_normal();
    while (path.filename().empty() && path.has_parent_path()) {
        const auto parent = path.parent_path();
        if (parent.empty() || parent == path) break;
        path = parent;
    }
    return path;
}

[[nodiscard]] inline std::optional<std::filesystem::path> latest_directory(
    const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    std::error_code error_code;
    if (!fs::is_directory(root, error_code) || error_code) return std::nullopt;

    std::vector<fs::path> directories;
    for (fs::directory_iterator iterator{root, error_code};
         !error_code && iterator != fs::directory_iterator{};
         iterator.increment(error_code)) {
        if (iterator->is_directory(error_code) && !error_code) {
            directories.push_back(iterator->path());
        }
    }
    if (error_code || directories.empty()) return std::nullopt;
    std::ranges::sort(
        directories,
        {},
        [](const fs::path& path) { return path.filename().native(); });
    return stable_path(directories.back());
}

[[nodiscard]] inline bool selected_sdk_version_is_latest(
    const std::span<const process::EnvironmentVariable> environment,
    const std::string_view root_name,
    const std::string_view version_name) {
    const auto* root = find_environment(environment, root_name);
    const auto* version = find_environment(environment, version_name);
    if (root == nullptr && version == nullptr) return true;
    if (root == nullptr || version == nullptr || root->value.empty() || version->value.empty()) {
        return false;
    }

    const auto selected = stable_path(path_from_utf8(version->value));
    if (selected.empty() || selected.filename().empty()) return false;
    const auto latest = latest_directory(
        stable_path(path_from_utf8(root->value)) / "Include");
    return latest
        && ascii_iequals(
            latest->filename().string(),
            selected.filename().string());
}

} // namespace toolchain_environment_detail

// cl.exe search identity. INCLUDE controls ordinary header lookup, LIBPATH
// controls #using metadata lookup, and PATH can affect compiler helper-tool
// discovery. The vcvars metadata names the selected toolset/SDK state from which
// those effective values were produced. LIB belongs to LINK and is deliberately
// excluded so a library-search-only mutation does not rebuild every translation
// unit. CL/_CL_ are absent because MQB masks those option injectors at launch.
[[nodiscard]] inline std::string compiler_environment_stamp(
    const std::span<const process::EnvironmentVariable> environment) {
    constexpr std::array names{
        std::string_view{"INCLUDE"},
        std::string_view{"LIBPATH"},
        std::string_view{"PATH"},
        std::string_view{"VCToolsInstallDir"},
        std::string_view{"WindowsSdkDir"},
        std::string_view{"WindowsSDKVersion"},
        std::string_view{"UniversalCRTSdkDir"},
        std::string_view{"UCRTVersion"},
        std::string_view{"NETFXSDKDir"},
    };
    return toolchain_environment_detail::environment_stamp(
        environment,
        names,
        "mqb.compiler.environment.v1");
}

// link.exe search identity. LIB is LINK's ambient library/object search list;
// PATH can affect linker helper-tool lookup. Typed /LIBPATH roots remain in
// LinkOptions/BuildSignature, while vcvars metadata protects the selected SDK
// state. LINK/_LINK_ are absent because MQB masks those option injectors.
[[nodiscard]] inline std::string linker_environment_stamp(
    const std::span<const process::EnvironmentVariable> environment) {
    constexpr std::array names{
        std::string_view{"LIB"},
        std::string_view{"PATH"},
        std::string_view{"VCToolsInstallDir"},
        std::string_view{"WindowsSdkDir"},
        std::string_view{"WindowsSDKVersion"},
        std::string_view{"UniversalCRTSdkDir"},
        std::string_view{"UCRTVersion"},
        std::string_view{"NETFXSDKDir"},
    };
    return toolchain_environment_detail::environment_stamp(
        environment,
        names,
        "mqb.linker.environment.v1");
}

// Core already treats ToolchainIdentity::binary_stamp as an opaque compiler
// backend identity component. Old cache entries naturally fail closed because
// they do not contain this compiler-environment suffix.
inline void seal_compiler_environment_identity(MsvcToolchain& toolchain) {
    toolchain.identity.binary_stamp += "|" + compiler_environment_stamp(toolchain.environment);
}

// A reused vcvars cache is only valid while its selected Windows SDK/UCRT
// versions remain the latest versions that a fresh vcvarsall invocation would
// select. Directory existence alone is insufficient: installing a newer SDK
// leaves the old paths valid but semantically stale.
[[nodiscard]] inline bool cached_visual_studio_environment_is_fresh(
    const MsvcToolchain& toolchain) {
    if (toolchain.source != ToolchainSource::visual_studio || !toolchain.reused) {
        return true;
    }
    return toolchain_environment_detail::selected_sdk_version_is_latest(
               toolchain.environment,
               "WindowsSdkDir",
               "WindowsSDKVersion")
        && toolchain_environment_detail::selected_sdk_version_is_latest(
               toolchain.environment,
               "UniversalCRTSdkDir",
               "UCRTVersion");
}

} // namespace mqb::msvc
