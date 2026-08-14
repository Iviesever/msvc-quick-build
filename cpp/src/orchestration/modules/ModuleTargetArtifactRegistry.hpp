#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>

#include "mqb/orchestration/MsvcModuleTargetCoordinator.hpp"

namespace mqb::orchestration::detail {

class ModuleTargetArtifactRegistry {
public:
    explicit ModuleTargetArtifactRegistry(std::size_t expected_source_count);

    [[nodiscard]] std::expected<void, IncrementalModuleTargetError>
    add_requested_source(const ModuleCompileSourceRequest& source);

    [[nodiscard]] std::expected<void, IncrementalModuleTargetError>
    add_target(const TargetArtifacts& target);

    [[nodiscard]] std::expected<void, IncrementalModuleTargetError>
    add_standard_library_source_identity(const std::filesystem::path& source);

    [[nodiscard]] std::expected<void, IncrementalModuleTargetError>
    add_standard_library_artifacts(
        const std::filesystem::path& source,
        const SourceArtifacts& artifacts);

    [[nodiscard]] std::expected<void, IncrementalModuleTargetError>
    add_header_unit_source(
        const std::filesystem::path& source,
        const SourceArtifacts& artifacts);

private:
    [[nodiscard]] std::expected<void, IncrementalModuleTargetError>
    claim(
        const std::filesystem::path& source,
        const std::filesystem::path& artifact,
        std::string_view role);

    std::unordered_set<std::string> seen_sources_;
    std::unordered_set<std::string> claimed_artifacts_;
};

} // namespace mqb::orchestration::detail
