#include "mqb/core/ProjectArtifactLayout.hpp"

#include <array>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
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

[[nodiscard]] char ascii_lower(const char value) noexcept {
    const unsigned char byte = static_cast<unsigned char>(value);
    return static_cast<char>(std::tolower(byte));
}

[[nodiscard]] std::string lower_ascii(std::string value) {
    for (char& ch : value) {
        ch = ascii_lower(ch);
    }
    return value;
}

[[nodiscard]] std::string path_text(const fs::path& path) {
    const auto bytes = path.lexically_normal().generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()};
}

[[nodiscard]] fs::path normalize_existing_identity(fs::path path) {
    std::error_code error_code;
    fs::path absolute = fs::absolute(path, error_code);
    if (!error_code) {
        path = std::move(absolute);
    }
    error_code.clear();
    fs::path canonical = fs::weakly_canonical(path, error_code);
    if (!error_code) {
        return canonical.lexically_normal();
    }
    return path.lexically_normal();
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

[[nodiscard]] std::uint64_t fnv1a64(std::string_view value) noexcept {
    std::uint64_t hash = 14695981039346656037ull;
    for (const unsigned char byte : value) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

[[nodiscard]] std::string hex64(const std::uint64_t value) {
    static constexpr std::array<char, 16> digits{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string result(16, '0');
    std::uint64_t remaining = value;
    for (std::size_t index = result.size(); index > 0; --index) {
        result[index - 1] = digits[remaining & 0x0fu];
        remaining >>= 4u;
    }
    return result;
}

[[nodiscard]] fs::path source_key(
    const fs::path& project_root,
    const fs::path& source) {
    const fs::path normalized_source = normalize_existing_identity(source);
    const fs::path normalized_root = normalize_existing_identity(project_root);
    const fs::path relative = normalized_source.lexically_relative(normalized_root);
    if (safe_relative(relative)) {
        return relative;
    }

    const std::string normalized = lower_ascii(path_text(normalized_source));
    const std::string filename = normalized_source.filename().string();
    return fs::path{"external"}
        / (hex64(fnv1a64(normalized)) + "_" + filename);
}

[[nodiscard]] bool valid_target_name(const std::string_view target_name) {
    if (target_name.empty()) return false;
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

std::expected<TargetArtifacts, ArtifactLayoutError>
ProjectArtifactLayout::for_target(
    const std::string_view target_name,
    const TargetKind target_kind) const {
    if (!valid_target_name(target_name)) {
        return std::unexpected(failure(
            ArtifactLayoutErrorCode::invalid_target_name,
            fs::path{std::string{target_name}},
            "target name must be one filename component"));
    }

    fs::path executable = artifact_root_ / "bin" / std::string{target_name};
    executable += target_suffix(target_kind);

    fs::path cache;
    if (target_kind == TargetKind::static_library) {
        cache = artifact_root_ / "cache" / "archive" / std::string{target_name};
        cache += ".archivecache";
    } else {
        cache = artifact_root_ / "cache" / "link" / std::string{target_name};
        cache += ".linkcache";
    }

    return TargetArtifacts{
        .executable = std::move(executable),
        .link_cache = std::move(cache),
    };
}

} // namespace mqb
