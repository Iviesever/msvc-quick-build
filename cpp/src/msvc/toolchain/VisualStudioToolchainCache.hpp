#pragma once

#include <filesystem>
#include <optional>

#include "mqb/msvc/MsvcToolchainLocator.hpp"

namespace mqb::msvc::detail {

// Pure raw destination policy shared with pre-write collection. No cwd lookup,
// filesystem access, or lexical parent traversal removal.
[[nodiscard]] std::optional<std::filesystem::path>
visual_studio_toolchain_cache_destination(const DiscoveryOptions& options);

[[nodiscard]] std::optional<std::filesystem::path>
visual_studio_toolchain_cache_file(const DiscoveryOptions& options);

[[nodiscard]] std::optional<MsvcToolchain>
reuse_visual_studio_toolchain_cache(
    const std::filesystem::path& cache_file,
    const DiscoveryOptions& options);

[[nodiscard]] std::optional<MsvcToolchain>
adopt_ambient_visual_studio_toolchain(
    process::ProcessRunner& runner,
    const DiscoveryOptions& options);

void save_visual_studio_toolchain_cache_best_effort(
    const std::filesystem::path& cache_file,
    const DiscoveryOptions& options,
    const MsvcToolchain& toolchain) noexcept;

} // namespace mqb::msvc::detail
