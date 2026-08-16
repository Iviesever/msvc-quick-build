#include "VisualStudioToolchainCache.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "ToolchainDiscoveryPrimitives.hpp"
#include "mqb/platform/windows/PathIdentity.hpp"

namespace mqb::msvc::detail {
namespace {

namespace fs = std::filesystem;
using process::EnvironmentVariable;

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

struct ToolPaths {
    fs::path compiler;
    fs::path linker;
    fs::path librarian;
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

[[nodiscard]] bool same_path(const fs::path& left, const fs::path& right) {
    return mqb::platform::windows::path_identity_key(left)
        == mqb::platform::windows::path_identity_key(right);
}

[[nodiscard]] int preference_value(const ToolchainPreference preference) noexcept {
    switch (preference) {
    case ToolchainPreference::automatic: return 0;
    case ToolchainPreference::visual_studio: return 1;
    case ToolchainPreference::portable: return 2;
    }
    return -1;
}

[[nodiscard]] std::string_view preference_name(const ToolchainPreference preference) noexcept {
    switch (preference) {
    case ToolchainPreference::automatic: return "auto";
    case ToolchainPreference::visual_studio: return "vs";
    case ToolchainPreference::portable: return "portable";
    }
    return "unknown";
}

[[nodiscard]] bool cache_key_matches(const CacheRecord& record, const DiscoveryOptions& options) {
    return record.target_architecture == detail::architecture_name(options.target_architecture)
        && record.host_architecture == detail::architecture_name(options.host_architecture)
        && record.preference == preference_value(options.preference);
}

[[nodiscard]] std::optional<fs::path> effective_cache_file(const DiscoveryOptions& options) {
    if (options.vswhere_path || options.cmd_path) return std::nullopt;
    if (options.cache_file) {
        if (options.cache_file->empty()) return std::nullopt;
        return stable_path(*options.cache_file);
    }
    const std::string filename =
        "msvc-" + std::string{preference_name(options.preference)} + "-"
        + detail::architecture_name(options.host_architecture) + "-"
        + detail::architecture_name(options.target_architecture) + ".mqbcache";
    return stable_path(fs::path{".mqb"} / "cache" / "toolchain" / filename);
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

[[nodiscard]] const EnvironmentVariable* find_environment(
    const std::vector<EnvironmentVariable>& environment,
    const std::string_view name) {
    const auto found = std::find_if(environment.begin(), environment.end(), [&](const EnvironmentVariable& variable) {
        return environment_name_equal(variable.name, name);
    });
    return found == environment.end() ? nullptr : &*found;
}

[[nodiscard]] fs::path visual_studio_root(const fs::path& vc_tools_root) {
    fs::path root = stable_path(vc_tools_root);
    for (int level = 0; level < 4 && root.has_parent_path(); ++level) root = root.parent_path();
    return stable_path(std::move(root));
}

[[nodiscard]] bool path_inside_root(const fs::path& root, const fs::path& candidate) {
    std::error_code error_code;
    const fs::path absolute_root = stable_path(fs::absolute(root, error_code));
    if (error_code) return false;
    const fs::path absolute_candidate = stable_path(fs::absolute(candidate, error_code));
    if (error_code) return false;
    return mqb::platform::windows::path_identity_contains(absolute_root, absolute_candidate);
}

void append_unique_existing_root(std::vector<fs::path>& roots, fs::path root) {
    std::error_code error_code;
    root = stable_path(std::move(root));
    if (!fs::is_directory(root, error_code) || error_code) return;
    const auto duplicate = std::find_if(roots.begin(), roots.end(), [&](const fs::path& existing) {
        return same_path(existing, root);
    });
    if (duplicate == roots.end()) roots.push_back(std::move(root));
}

[[nodiscard]] std::vector<fs::path> trusted_roots(
    const std::vector<EnvironmentVariable>& environment,
    const fs::path& vc_tools_root) {
    std::vector<fs::path> roots;
    append_unique_existing_root(roots, visual_studio_root(vc_tools_root));
    for (const auto name : {"WindowsSdkDir", "UniversalCRTSdkDir", "NETFXSDKDir"}) {
        if (const auto* variable = find_environment(environment, name);
            variable != nullptr && !variable->value.empty()) {
            append_unique_existing_root(roots, detail::path_from_utf8(variable->value));
        }
    }
    if (const auto system_root = detail::environment_path("SystemRoot")) append_unique_existing_root(roots, *system_root);
    return roots;
}

[[nodiscard]] bool path_inside_any_root(const std::vector<fs::path>& roots, const fs::path& candidate) {
    return std::any_of(roots.begin(), roots.end(), [&](const fs::path& root) {
        return path_inside_root(root, candidate);
    });
}

[[nodiscard]] std::vector<std::string_view> split_semicolon(const std::string& value) {
    std::vector<std::string_view> parts;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const std::size_t end = value.find(';', begin);
        const std::size_t count = end == std::string::npos ? value.size() - begin : end - begin;
        if (count != 0) parts.push_back(std::string_view{value}.substr(begin, count));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return parts;
}

[[nodiscard]] bool trusted_directory_list(const std::string& value, const std::vector<fs::path>& roots) {
    const auto parts = split_semicolon(value);
    if (parts.empty()) return false;
    for (const auto part : parts) {
        fs::path directory = stable_path(detail::path_from_utf8(part));
        std::error_code error_code;
        if (!fs::is_directory(directory, error_code) || error_code || !path_inside_any_root(roots, directory)) return false;
    }
    return true;
}

[[nodiscard]] std::vector<EnvironmentVariable> cacheable_environment(const MsvcToolchain& toolchain) {
    std::vector<EnvironmentVariable> result;
    for (const auto& variable : toolchain.environment) {
        if (cacheable_environment_name(variable.name)) result.push_back(variable);
    }
    return result;
}

[[nodiscard]] bool environment_is_cacheable(
    const std::vector<EnvironmentVariable>& environment,
    const fs::path& vc_tools_root) {
    const auto* include = find_environment(environment, "INCLUDE");
    const auto* lib = find_environment(environment, "LIB");
    const auto* lib_path = find_environment(environment, "LIBPATH");
    const auto* tools = find_environment(environment, "VCToolsInstallDir");
    if (include == nullptr || lib == nullptr || lib_path == nullptr || tools == nullptr
        || include->value.empty() || lib->value.empty() || lib_path->value.empty() || tools->value.empty()) return false;
    if (!same_path(detail::path_from_utf8(tools->value), vc_tools_root)) return false;
    const auto roots = trusted_roots(environment, vc_tools_root);
    return !roots.empty()
        && trusted_directory_list(include->value, roots)
        && trusted_directory_list(lib->value, roots)
        && trusted_directory_list(lib_path->value, roots);
}

[[nodiscard]] ToolPaths tool_paths(const fs::path& vc_tools_root, const DiscoveryOptions& options) {
    const fs::path directory = stable_path(vc_tools_root) / "bin"
        / detail::host_directory(options.host_architecture)
        / detail::architecture_name(options.target_architecture);
    return ToolPaths{
        .compiler = stable_path(directory / "cl.exe"),
        .linker = stable_path(directory / "link.exe"),
        .librarian = stable_path(directory / "lib.exe"),
    };
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

[[nodiscard]] std::optional<MsvcToolchain> try_reuse_visual_studio_cache(
    const fs::path& cache_file,
    const DiscoveryOptions& options) {
    try {
        std::error_code error_code;
        if (!fs::is_regular_file(cache_file, error_code) || error_code) return std::nullopt;
        const auto size = fs::file_size(cache_file, error_code);
        if (error_code || size > max_cache_size) return std::nullopt;
        const auto modified = fs::last_write_time(cache_file, error_code);
        if (error_code) return std::nullopt;
        const auto now = fs::file_time_type::clock::now();
        if (modified > now || now - modified > max_cache_age) return std::nullopt;
        std::ifstream stream{cache_file, std::ios::binary};
        if (!stream) return std::nullopt;
        auto record = read_record(stream);
        if (!record || !cache_key_matches(*record, options) || record->binary_stamp.empty()) return std::nullopt;

        const std::string current_ambient_path = detail::environment_value("PATH").value_or(std::string{});
        if (record->ambient_path != current_ambient_path) {
            // PATH is part of compiler/linker effective identity. Never replay a
            // vcvars result against a different launching PATH; rediscover it.
            return std::nullopt;
        }

        if (!fs::is_directory(record->vc_tools_root, error_code) || error_code) return std::nullopt;
        const auto latest_tools = detail::latest_directory(record->vc_tools_root.parent_path());
        if (!latest_tools || !same_path(*latest_tools, record->vc_tools_root)) return std::nullopt;
        const ToolPaths paths = tool_paths(record->vc_tools_root, options);
        for (const auto& file : {paths.compiler, paths.linker, paths.librarian}) {
            error_code.clear();
            if (!fs::is_regular_file(file, error_code) || error_code) return std::nullopt;
        }
        auto stamp = detail::binary_stamp(paths.compiler);
        if (!stamp || *stamp != record->binary_stamp) return std::nullopt;
        if (!environment_is_cacheable(record->environment, record->vc_tools_root)) return std::nullopt;

        // PATH is opaque vcvars output, not a set of MQB-owned roots. Persist it
        // byte-for-byte and bind it to the exact ambient PATH that produced it;
        // attempting to classify every vcvars-added entry against an incomplete
        // trusted-root list caused valid discovery caches to be discarded.
        std::vector<EnvironmentVariable> environment = std::move(record->environment);
        environment.push_back(EnvironmentVariable{.name = "PATH", .value = std::move(record->effective_path)});
        return MsvcToolchain{
            .identity = ToolchainIdentity{
                .compiler = paths.compiler,
                .version = detail::path_to_utf8(record->vc_tools_root.filename()),
                .binary_stamp = std::move(record->binary_stamp),
            },
            .linker = paths.linker,
            .librarian = paths.librarian,
            .vc_tools_root = record->vc_tools_root,
            .standard_library_modules = detail::discover_standard_library_modules(record->vc_tools_root),
            .source = ToolchainSource::visual_studio,
            .environment = std::move(environment),
            .reused = true,
        };
    } catch (...) {
        return std::nullopt;
    }
}

void save_visual_studio_cache_best_effort(
    const fs::path& cache_file,
    const DiscoveryOptions& options,
    const MsvcToolchain& toolchain) noexcept {
    try {
        if (toolchain.source != ToolchainSource::visual_studio || toolchain.reused) return;
        const fs::path root = stable_path(toolchain.vc_tools_root);
        const ToolPaths expected = tool_paths(root, options);
        if (!same_path(expected.compiler, toolchain.identity.compiler)
            || !same_path(expected.linker, toolchain.linker)
            || !same_path(expected.librarian, toolchain.librarian)) return;

        std::vector<EnvironmentVariable> environment = cacheable_environment(toolchain);
        const auto* path = find_environment(toolchain.environment, "PATH");
        if (path == nullptr || path->value.empty()) return;
        const std::string ambient_path = detail::environment_value("PATH").value_or(std::string{});
        if (!environment_is_cacheable(environment, root)) return;

        const CacheRecord record{
            .target_architecture = detail::architecture_name(options.target_architecture),
            .host_architecture = detail::architecture_name(options.host_architecture),
            .preference = preference_value(options.preference),
            .vc_tools_root = root,
            .binary_stamp = toolchain.identity.binary_stamp,
            .environment = std::move(environment),
            .ambient_path = ambient_path,
            .effective_path = path->value,
        };
        std::error_code error_code;
        if (!cache_file.parent_path().empty()) {
            fs::create_directories(cache_file.parent_path(), error_code);
            if (error_code) return;
        }
        fs::path temporary = cache_file;
        temporary += ".tmp." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        {
            std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};
            if (!stream || !write_record(stream, record)) {
                stream.close();
                fs::remove(temporary, error_code);
                return;
            }
            stream.flush();
            if (!stream) {
                stream.close();
                fs::remove(temporary, error_code);
                return;
            }
        }
        error_code.clear();
        if (fs::exists(cache_file, error_code) && !error_code) {
            fs::remove(cache_file, error_code);
            if (error_code) {
                fs::remove(temporary, error_code);
                return;
            }
        } else if (error_code) {
            fs::remove(temporary, error_code);
            return;
        }
        fs::rename(temporary, cache_file, error_code);
        if (error_code) {
            std::error_code ignored;
            fs::remove(temporary, ignored);
        }
    } catch (...) {
        return;
    }
}

} // namespace

std::optional<std::filesystem::path>
visual_studio_toolchain_cache_file(const DiscoveryOptions& options) {
    return effective_cache_file(options);
}

std::optional<MsvcToolchain>
reuse_visual_studio_toolchain_cache(
    const std::filesystem::path& cache_file,
    const DiscoveryOptions& options) {
    return try_reuse_visual_studio_cache(cache_file, options);
}

void save_visual_studio_toolchain_cache_best_effort(
    const std::filesystem::path& cache_file,
    const DiscoveryOptions& options,
    const MsvcToolchain& toolchain) noexcept {
    save_visual_studio_cache_best_effort(cache_file, options, toolchain);
}

} // namespace mqb::msvc::detail
