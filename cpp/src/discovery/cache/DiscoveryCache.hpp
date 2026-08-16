#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

#include "mqb/core/FileSnapshot.hpp"
#include "mqb/discovery/SourceDiscovery.hpp"
#include "mqb/platform/windows/PathIdentity.hpp"

namespace mqb::discovery::detail {

struct DiscoveryRequestIdentity {
    std::filesystem::path project_root;
    std::filesystem::path entry;
    std::vector<std::filesystem::path> include_directories;
    std::vector<std::filesystem::path> forced_includes;
    std::vector<std::filesystem::path> excluded_directories;
    std::vector<std::filesystem::path> extra_sources;
    std::vector<std::filesystem::path> excluded_sources;

    [[nodiscard]] bool operator==(const DiscoveryRequestIdentity& other) const {
        const auto same_path = [](const std::filesystem::path& left, const std::filesystem::path& right) {
            return mqb::platform::windows::path_identity_key(left)
                == mqb::platform::windows::path_identity_key(right);
        };
        const auto same_paths = [&](
                                    const std::vector<std::filesystem::path>& left,
                                    const std::vector<std::filesystem::path>& right) {
            if (left.size() != right.size()) {
                return false;
            }
            for (std::size_t index = 0; index < left.size(); ++index) {
                if (!same_path(left[index], right[index])) {
                    return false;
                }
            }
            return true;
        };

        return same_path(project_root, other.project_root)
            && same_path(entry, other.entry)
            && same_paths(include_directories, other.include_directories)
            && same_paths(forced_includes, other.forced_includes)
            && same_paths(excluded_directories, other.excluded_directories)
            && same_paths(extra_sources, other.extra_sources)
            && same_paths(excluded_sources, other.excluded_sources);
    }
};

struct DiscoveryCacheRecord {
    DiscoveryRequestIdentity request;
    Result result;
    std::vector<FileSnapshot> files;
    std::vector<FileSnapshot> directories;
};

[[nodiscard]] std::optional<Result> try_reuse_discovery_cache(
    const std::filesystem::path& cache_file,
    const DiscoveryRequestIdentity& request) noexcept;

void save_discovery_cache_best_effort(
    const std::filesystem::path& cache_file,
    const DiscoveryCacheRecord& record) noexcept;

} // namespace mqb::discovery::detail
