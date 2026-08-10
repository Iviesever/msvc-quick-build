#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace mqb {

struct SourceArtifacts {
    std::filesystem::path object;
    std::filesystem::path dependencies;
    std::filesystem::path compile_cache;
};

struct TargetArtifacts {
    std::filesystem::path executable;
    std::filesystem::path link_cache;
};

enum class ArtifactLayoutErrorCode {
    empty_project_root,
    empty_source,
    invalid_source_filename,
    invalid_target_name,
};

struct ArtifactLayoutError {
    ArtifactLayoutErrorCode code{ArtifactLayoutErrorCode::empty_project_root};
    std::filesystem::path path;
    std::string message;
};

class ProjectArtifactLayout {
public:
    [[nodiscard]] static std::expected<ProjectArtifactLayout, ArtifactLayoutError>
    create(std::filesystem::path project_root);

    [[nodiscard]] std::expected<SourceArtifacts, ArtifactLayoutError>
    for_source(const std::filesystem::path& source) const;

    [[nodiscard]] std::expected<TargetArtifacts, ArtifactLayoutError>
    for_target(std::string_view target_name) const;

    [[nodiscard]] const std::filesystem::path& project_root() const noexcept {
        return project_root_;
    }

    [[nodiscard]] const std::filesystem::path& artifact_root() const noexcept {
        return artifact_root_;
    }

private:
    ProjectArtifactLayout(
        std::filesystem::path project_root,
        std::filesystem::path artifact_root)
        : project_root_(std::move(project_root)),
          artifact_root_(std::move(artifact_root)) {}

    std::filesystem::path project_root_;
    std::filesystem::path artifact_root_;
};

} // namespace mqb
