#pragma once

#include <expected>
#include <filesystem>

#include "mqb/msvc/MsvcToolchainLocator.hpp"

namespace mqb::msvc::detail {

[[nodiscard]] std::expected<MsvcToolchain, ToolchainError> discover_portable_toolchain(
    const std::filesystem::path& portable_root,
    const DiscoveryOptions& options);

} // namespace mqb::msvc::detail
