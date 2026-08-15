#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mqb/core/BuildTypes.hpp"
#include "mqb/core/ToolchainIdentity.hpp"
#include "mqb/process/Process.hpp"

namespace mqb::msvc {

enum class ToolchainSource {
    visual_studio,
    portable,
};

enum class ToolchainPreference {
    automatic,
    visual_studio,
    portable,
};

struct StandardLibraryModuleSources {
    std::optional<std::filesystem::path> std;
    std::optional<std::filesystem::path> std_compat;
};

struct DiscoveryOptions {
    Architecture target_architecture{Architecture::x64};
    Architecture host_architecture{Architecture::x64};
    ToolchainPreference preference{ToolchainPreference::automatic};
    std::vector<std::filesystem::path> portable_roots;
    std::optional<std::filesystem::path> vswhere_path;
    std::optional<std::filesystem::path> cmd_path;
    // Visual Studio discovery only. nullopt uses MQB's user-local cache under
    // LOCALAPPDATA; an explicitly empty path disables persistent reuse. Custom
    // vswhere/cmd overrides also disable reuse so explicit discovery remains authoritative.
    std::optional<std::filesystem::path> cache_file;
};

struct MsvcToolchain {
    ToolchainIdentity identity;
    std::filesystem::path linker;
    std::filesystem::path librarian;
    std::filesystem::path vc_tools_root;
    // The selected VC Tools version owns these sources. Missing files do not
    // invalidate the compiler toolchain itself; module-target routing reports a
    // capability-specific diagnostic only when std/std.compat is actually
    // required by P1689 metadata.
    StandardLibraryModuleSources standard_library_modules;
    ToolchainSource source{ToolchainSource::visual_studio};
    std::vector<process::EnvironmentVariable> environment;
    // True only when validated persistent Visual Studio discovery evidence was
    // reused without invoking vswhere/cmd/vcvarsall for this call.
    bool reused{false};
};

enum class ToolchainErrorCode {
    toolchain_not_found,
    invalid_portable_layout,
    visual_studio_environment_failed,
    compiler_not_found,
    process_failed,
    invalid_environment_output,
};

struct ToolchainError {
    ToolchainErrorCode code{ToolchainErrorCode::toolchain_not_found};
    std::filesystem::path path;
    std::string message;
    std::optional<process::ProcessError> process_error;
};

class MsvcToolchainLocator {
public:
    explicit MsvcToolchainLocator(process::ProcessRunner& runner) : runner_(runner) {}

    [[nodiscard]] std::expected<MsvcToolchain, ToolchainError>
    discover(const DiscoveryOptions& options) const;

private:
    process::ProcessRunner& runner_;
};

} // namespace mqb::msvc
