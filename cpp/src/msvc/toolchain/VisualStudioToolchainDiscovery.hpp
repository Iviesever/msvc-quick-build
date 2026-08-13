#pragma once

#include <expected>

#include "mqb/msvc/MsvcToolchainLocator.hpp"

namespace mqb::msvc::detail {

[[nodiscard]] std::expected<MsvcToolchain, ToolchainError>
discover_visual_studio_toolchain(
    process::ProcessRunner& runner,
    const DiscoveryOptions& options);

} // namespace mqb::msvc::detail
