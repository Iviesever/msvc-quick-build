#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

#include "mqb/core/FileSnapshot.hpp"
#include "mqb/discovery/SourceDiscovery.hpp"

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
        if (project_root != other.project_root
            || entry != other.entry
            || include_directories != other.include_directories
            || excluded_directories != other.excluded_directories
            || extra_sources != other.extra_sources
            || excluded_sources != other.excluded_sources
            || forced_includes.size() != other.forced_includes.size()) {
            return false;
        }
        for (std::size_t index = 0; index < forced_includes.size(); ++index) {
            if (forced_includes[index].lexically_normal()
                != other.forced_includes[index].lexically_normal()) {
                return false;
            }
        }
        return true;
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
