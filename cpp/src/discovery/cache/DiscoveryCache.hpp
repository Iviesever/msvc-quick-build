#pragma once

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
    std::vector<std::filesystem::path> excluded_directories;
    std::vector<std::filesystem::path> extra_sources;
    std::vector<std::filesystem::path> excluded_sources;

    bool operator==(const DiscoveryRequestIdentity&) const = default;
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
