#include "ModuleTargetArtifactRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

namespace mqb::orchestration::detail {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string windows_path_key(const fs::path& path) {
    std::string value = path.lexically_normal().generic_string();
    std::transform(
        value.begin(), value.end(), value.begin(),
        [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

[[nodiscard]] IncrementalModuleTargetError failure(
    const IncrementalModuleTargetErrorCode code,
    std::string message,
    fs::path source = {}) {
    return IncrementalModuleTargetError{
        .code = code,
        .message = std::move(message),
        .source = std::move(source),
    };
}

[[nodiscard]] IncrementalModuleTargetError artifact_failure(
    const IncrementalModuleTargetErrorCode code,
    std::string message,
    const fs::path& source,
    const fs::path& artifact) {
    return IncrementalModuleTargetError{
        .code = code,
        .message = std::move(message),
        .source = source,
        .artifact = artifact,
    };
}

} // namespace

ModuleTargetArtifactRegistry::ModuleTargetArtifactRegistry(
    const std::size_t expected_source_count) {
    seen_sources_.reserve(expected_source_count + 2u);
    claimed_artifacts_.reserve(expected_source_count * 5u + 18u);
}

std::expected<void, IncrementalModuleTargetError>
ModuleTargetArtifactRegistry::claim(
    const fs::path& source,
    const fs::path& artifact,
    const std::string_view role) {
    if (artifact.empty()) {
        return std::unexpected(artifact_failure(
            IncrementalModuleTargetErrorCode::invalid_artifact,
            "module target " + std::string{role} + " artifact path is empty",
            source,
            artifact));
    }
    if (!claimed_artifacts_.emplace(windows_path_key(artifact)).second) {
        return std::unexpected(artifact_failure(
            IncrementalModuleTargetErrorCode::artifact_collision,
            "module target " + std::string{role}
                + " artifact collides with another planned writable artifact",
            source,
            artifact));
    }
    return {};
}

std::expected<void, IncrementalModuleTargetError>
ModuleTargetArtifactRegistry::add_requested_source(
    const ModuleCompileSourceRequest& source) {
    if (source.source.empty()) {
        return std::unexpected(failure(
            IncrementalModuleTargetErrorCode::duplicate_source,
            "module target source path is empty",
            source.source));
    }
    if (!seen_sources_.emplace(windows_path_key(source.source)).second) {
        return std::unexpected(failure(
            IncrementalModuleTargetErrorCode::duplicate_source,
            "module target contains the same source more than once",
            source.source));
    }

    if (auto result = claim(source.source, source.artifacts.object, "object"); !result) {
        return result;
    }
    if (auto result = claim(
            source.source,
            source.artifacts.dependencies,
            "source-dependency metadata"); !result) {
        return result;
    }
    if (auto result = claim(
            source.source,
            source.artifacts.module_dependencies,
            "module-scan metadata"); !result) {
        return result;
    }
    if (auto result = claim(
            source.source,
            source.artifacts.compile_cache,
            "compile-cache metadata"); !result) {
        return result;
    }
    if (source.kind == TranslationUnitKind::module_interface) {
        if (auto result = claim(
                source.source,
                source.artifacts.module_interface,
                "IFC"); !result) {
            return result;
        }
    }
    return {};
}

std::expected<void, IncrementalModuleTargetError>
ModuleTargetArtifactRegistry::add_target(const TargetArtifacts& target) {
    if (auto result = claim({}, target.executable, "executable"); !result) {
        return result;
    }
    return claim({}, target.link_cache, "link-cache metadata");
}

std::expected<void, IncrementalModuleTargetError>
ModuleTargetArtifactRegistry::add_standard_library_source_identity(
    const fs::path& source) {
    if (!seen_sources_.emplace(windows_path_key(source)).second) {
        return std::unexpected(failure(
            IncrementalModuleTargetErrorCode::duplicate_source,
            "toolchain standard-library module source collides with another target source",
            source));
    }
    return {};
}

std::expected<void, IncrementalModuleTargetError>
ModuleTargetArtifactRegistry::add_standard_library_artifacts(
    const fs::path& source,
    const SourceArtifacts& artifacts) {
    if (auto result = claim(source, artifacts.object, "standard-library module object"); !result) {
        return result;
    }
    if (auto result = claim(
            source,
            artifacts.dependencies,
            "standard-library source-dependency metadata"); !result) {
        return result;
    }
    if (auto result = claim(
            source,
            artifacts.module_dependencies,
            "standard-library module-scan metadata"); !result) {
        return result;
    }
    if (auto result = claim(
            source,
            artifacts.compile_cache,
            "standard-library compile-cache metadata"); !result) {
        return result;
    }
    return claim(source, artifacts.module_interface, "standard-library IFC");
}

std::expected<void, IncrementalModuleTargetError>
ModuleTargetArtifactRegistry::add_header_unit_source(
    const fs::path& source,
    const SourceArtifacts& artifacts) {
    // Header units are IFC-only producers. Object and module-scan paths from
    // SourceArtifacts remain reserved layout slots but are not writable inputs
    // to this target, so register only the artifacts this producer writes.
    if (auto result = claim(
            source,
            artifacts.dependencies,
            "header-unit source-dependency metadata"); !result) {
        return result;
    }
    if (auto result = claim(
            source,
            artifacts.compile_cache,
            "header-unit compile-cache metadata"); !result) {
        return result;
    }
    return claim(source, artifacts.module_interface, "header-unit IFC");
}

} // namespace mqb::orchestration::detail
