#include "mqb/core/ProjectArtifactLayout.hpp"

#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "mqb/platform/windows/PathIdentity.hpp"

namespace mqb {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] fs::path path_from_utf8(const std::string_view value) {
    std::u8string bytes;
    bytes.assign(
        reinterpret_cast<const char8_t*>(value.data()),
        reinterpret_cast<const char8_t*>(value.data() + value.size()));
    return fs::path{bytes};
}

[[nodiscard]] ArtifactLayoutError failure(
    const ArtifactLayoutErrorCode code,
    fs::path path,
    std::string message) {
    return ArtifactLayoutError{
        .code = code,
        .path = std::move(path),
        .message = std::move(message),
    };
}

[[nodiscard]] fs::path normalize_existing_identity(const fs::path& path) {
    fs::path normalized = path.lexically_normal();
    std::error_code error_code;
    if (!fs::exists(normalized, error_code) || error_code) {
        return normalized;
    }

    error_code.clear();
    fs::path canonical = fs::weakly_canonical(normalized, error_code);
    if (error_code || canonical.empty()) {
        return normalized;
    }
    return canonical.lexically_normal();
}

[[nodiscard]] fs::path identity_path(const fs::path& path) {
    const std::string key = platform::windows::path_identity_key(path);
    return path_from_utf8(key);
}

[[nodiscard]] bool safe_relative(const fs::path& relative) {
    if (relative.empty() || relative.is_absolute() || relative == ".") {
        return false;
    }
    for (const auto& component : relative) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<fs::path> physical_relative(
    const fs::path& project_root,
    const fs::path& source) {
    std::error_code error_code;
    if (!fs::exists(project_root, error_code) || error_code) return std::nullopt;
    error_code.clear();
    if (!fs::exists(source, error_code) || error_code) return std::nullopt;

    fs::path current = source.parent_path();
    fs::path relative = source.filename();
    while (!current.empty()) {
        error_code.clear();
        if (fs::equivalent(current, project_root, error_code) && !error_code) {
            return identity_path(relative.lexically_normal());
        }
        const fs::path parent = current.parent_path();
        if (parent.empty() || parent == current) break;
        relative = current.filename() / relative;
        current = parent;
    }
    return std::nullopt;
}

[[nodiscard]] std::string stable_path_hash(const fs::path& path) {
    constexpr std::uint64_t offset = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    std::uint64_t hash = offset;
    const std::string identity = platform::windows::path_identity_key(path);
    for (const unsigned char byte : identity) {
        hash ^= static_cast<std::uint8_t>(byte);
        hash *= prime;
    }

    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
}

[[nodiscard]] fs::path source_key(
    const fs::path& project_root,
    const fs::path& source) {
    const fs::path normalized_source = normalize_existing_identity(source);

    if (auto relative = physical_relative(project_root, normalized_source)) {
        return *relative;
    }

    fs::path relative = normalized_source.lexically_relative(project_root);
    if (safe_relative(relative)) {
        return identity_path(relative);
    }

    const fs::path identity = identity_path(normalized_source);
    return fs::path{".external"}
        / stable_path_hash(normalized_source)
        / identity.filename();
}

[[nodiscard]] bool valid_target_name(const std::string_view target_name) {
    if (target_name.empty()) return false;
    const fs::path path = path_from_utf8(target_name);
    return !path.has_root_path()
        && !path.has_parent_path()
        && path.filename() == path
        && path != "."
        && path != "..";
}

[[nodiscard]] fs::path append_suffix(fs::path path, const std::string_view suffix) {
    path += std::string{suffix};
    return path;
}

[[nodiscard]] std::string_view target_suffix(const TargetKind target_kind) {
    switch (target_kind) {
    case TargetKind::executable:
        return ".exe";
    case TargetKind::dynamic_library:
        return ".dll";
    case TargetKind::static_library:
        return ".lib";
    }
    return ".exe";
}

[[nodiscard]] std::string_view configuration_name(const BuildConfiguration configuration) {
    switch (configuration) {
    case BuildConfiguration::debug: return "debug";
    case BuildConfiguration::release: return "release";
    }
    return "debug";
}

[[nodiscard]] std::string_view architecture_name(const Architecture architecture) {
    switch (architecture) {
    case Architecture::x86: return "x86";
    case Architecture::x64: return "x64";
    }
    return "x64";
}

} // namespace

std::expected<ProjectArtifactLayout, ArtifactLayoutError>
ProjectArtifactLayout::create(fs::path project_root) {
    if (project_root.empty()) {
        return std::unexpected(failure(
            ArtifactLayoutErrorCode::empty_project_root,
            {},
            "project root must not be empty"));
    }
    project_root = normalize_existing_identity(project_root);
    return ProjectArtifactLayout{
        project_root,
        project_root / ".mqb",
    };
}

std::expected<SourceArtifacts, ArtifactLayoutError>
ProjectArtifactLayout::for_source(const fs::path& source) const {
    if (source.empty()) {
        return std::unexpected(failure(
            ArtifactLayoutErrorCode::empty_source,
            source,
            "source path must not be empty"));
    }
    if (source.filename().empty()) {
        return std::unexpected(failure(
            ArtifactLayoutErrorCode::invalid_source_filename,
            source,
            "source path must name a file"));
    }

    const fs::path key = source_key(project_root_, source);
    return SourceArtifacts{
        .object = append_suffix(artifact_root_ / "obj" / key, ".obj"),
        .dependencies = append_suffix(artifact_root_ / "deps" / key, ".json"),
        .module_dependencies = append_suffix(artifact_root_ / "scan" / key, ".json"),
        .module_interface = append_suffix(artifact_root_ / "ifc" / key, ".ifc"),
        .compile_cache = append_suffix(
            artifact_root_ / "cache" / "compile" / key,
            ".mqbcache"),
    };
}

std::expected<PrecompiledHeaderArtifacts, ArtifactLayoutError>
ProjectArtifactLayout::for_precompiled_header(
    const std::string_view target_name,
    const BuildConfiguration configuration,
    const Architecture architecture) const {
    const fs::path target_path = path_from_utf8(target_name);
    if (!valid_target_name(target_name)) {
        return std::unexpected(failure(
            ArtifactLayoutErrorCode::invalid_target_name,
            target_path,
            "target name must be one filename component"));
    }

    const fs::path key = target_path
        / std::string{configuration_name(configuration)}
        / std::string{architecture_name(architecture)};
    const fs::path root = artifact_root_ / "pch" / key;
    return PrecompiledHeaderArtifacts{
        .source = root / "creator.cpp",
        .object = root / "creator.obj",
        .dependencies = root / "creator.deps.json",
        .precompiled_header = root / "project.pch",
        .compile_cache = artifact_root_ / "cache" / "pch" / key / "creator.mqbcache",
    };
}

std::expected<TargetArtifacts, ArtifactLayoutError>
ProjectArtifactLayout::for_target(
    const std::string_view target_name,
    const TargetKind target_kind) const {
    const fs::path target_path = path_from_utf8(target_name);
    if (!valid_target_name(target_name)) {
        return std::unexpected(failure(
            ArtifactLayoutErrorCode::invalid_target_name,
            target_path,
            "target name must be one filename component"));
    }

    fs::path executable = artifact_root_ / "bin" / target_path;
    executable += target_suffix(target_kind);

    fs::path target_cache;
    if (target_kind == TargetKind::static_library) {
        target_cache = artifact_root_ / "cache" / "archive" / target_path;
        target_cache += ".archivecache";
    } else {
        target_cache = artifact_root_ / "cache" / "link" / target_path;
        target_cache += ".linkcache";
    }

    return TargetArtifacts{
        .executable = std::move(executable),
        .link_cache = std::move(target_cache),
    };
}

} // namespace mqb
