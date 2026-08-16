#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "PerformanceTimings.hpp"
#include "mqb/core/BuildRequest.hpp"
#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/LinkOptions.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/orchestration/ParallelismPolicy.hpp"

namespace mqb::cli {

enum class Command {
    direct,
    build,
    run,
};

struct Options {
    BuildRequest build;
    Command command{Command::direct};
    std::optional<std::string> profile;
    std::optional<BuildConfiguration> configuration_override;
    std::optional<Architecture> architecture_override;
    std::optional<CppStandard> standard_override;
    std::optional<TargetKind> target_kind_override;
    std::optional<RuntimeLibrary> runtime_override;
    std::optional<bool> ltcg_override;
    std::optional<LinkSubsystem> subsystem_override;
    std::optional<PrecompiledHeaderPolicy> pch_override;
    std::vector<std::string> defines;
    std::vector<std::filesystem::path> include_directories;
    std::vector<std::filesystem::path> discovery_include_directories;
    std::vector<std::filesystem::path> library_directories;
    std::vector<std::string> libraries;
    std::vector<std::string> compiler_arguments;
    std::vector<std::string> linker_arguments;
    std::vector<std::string> librarian_arguments;
    std::vector<ExternalModuleProvider> external_module_providers;
    msvc::ToolchainPreference toolchain_preference{msvc::ToolchainPreference::automatic};
    std::vector<std::filesystem::path> portable_roots;
    std::optional<bool> discovery_override;
    std::optional<orchestration::ParallelismPolicy> jobs;
    app::performance::Format timings{app::performance::Format::disabled};
    bool discover_sources{true};
    bool verbose{false};
    bool show_help{false};
};

struct Error {
    std::string message;
};

[[nodiscard]] std::expected<Options, Error>
parse_arguments(std::span<const std::string_view> arguments);

[[nodiscard]] std::string_view usage() noexcept;

} // namespace mqb::cli
