#include "mqb/core/ProjectArtifactLayout.hpp"

#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace mqb {
namespace {

namespace fs = std::filesystem;

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

[[nodiscard]] std::string stable_path_hash(const fs::path& path) {
    constexpr std::uint64_t offset = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    std::uint64_t hash = offset;
    const auto bytes = path.lexically_normal().generic_u8string();
    for (const char8_t byte : bytes) {
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
    const fs::path normalized_source = source.lexically_normal();
    const fs::path relative = normalized_source.lexically_relative(project_root);
    if (safe_relative(relative)) {
        return relative;
    }
    return fs::path{".external"}
        / stable_path_hash(normalized_source)
        / normalized_source.filename();
}

[[nodiscard]] bool valid_target_name(const std::string_view target_name) {
    if (target_name.empty()) {
        return false;
    }
    const fs::path path{std::string{target_name}};
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

} // namespace

std::expected<ProjectArtifactLayout, ArtifactLayoutError>
ProjectArtifactLayout::create(fs::path project_root) {
    if (project_root.empty()) {
        return std::unexpected(failure(
            ArtifactLayoutErrorCode::empty_project_root,
            {},
            "project root must not be empty"));
    }
    project_root = project_root.lexically_normal();
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

std::expected<TargetArtifacts, ArtifactLayoutError>
ProjectArtifactLayout::for_target(const std::string_view target_name) const {
    if (!valid_target_name(target_name)) {
        return std::unexpected(failure(
            ArtifactLayoutErrorCode::invalid_target_name,
            fs::path{std::string{target_name}},
            "target name must be one filename component"));
    }

    fs::path executable = artifact_root_ / "bin" / std::string{target_name};
    executable += ".exe";
    fs::path link_cache = artifact_root_ / "cache" / "link" / std::string{target_name};
    link_cache += ".linkcache";
    return TargetArtifacts{
        .executable = std::move(executable),
        .link_cache = std::move(link_cache),
    };
}

} // namespace mqb
