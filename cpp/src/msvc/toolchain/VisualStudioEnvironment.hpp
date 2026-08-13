#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <vector>

#include "mqb/msvc/MsvcToolchainLocator.hpp"

namespace mqb::msvc::detail {

struct CapturedVisualStudioEnvironment {
    std::vector<process::EnvironmentVariable> variables;
    std::optional<std::filesystem::path> vc_tools_root;
};

[[nodiscard]] std::filesystem::path default_command_processor();

[[nodiscard]] std::expected<CapturedVisualStudioEnvironment, ToolchainError>
capture_visual_studio_environment(
    process::ProcessRunner& runner,
    const std::filesystem::path& command_processor,
    const std::filesystem::path& vcvarsall,
    Architecture target_architecture);

} // namespace mqb::msvc::detail
