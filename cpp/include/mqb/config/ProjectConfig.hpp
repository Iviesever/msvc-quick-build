#pragma once

#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/LinkOptions.hpp"

namespace mqb::config {

struct BuildOverrides {
    std::optional<BuildConfiguration> configuration;
    std::optional<Architecture> architecture;
    std::optional<CppStandard> standard;
    std::optional<RuntimeLibrary> runtime_library;
    std::optional<bool> link_time_code_generation;
    std::optional<LinkSubsystem> subsystem;
    std::optional<TargetKind> target_kind;
    std::optional<std::filesystem::path> entry;
    std::optional<std::string> output_name;
    std::vector<std::string> defines;
    std::vector<std::filesystem::path> include_directories;
    std::vector<std::filesystem::path> library_directories;
    std::vector<std::string> libraries;
    std::vector<std::string> compiler_arguments;
    std::vector<std::string> linker_arguments;
};

struct DiscoveryOverrides {
    std::optional<bool> enabled;
    std::vector<std::filesystem::path> exclude_directories;
    std::vector<std::filesystem::path> extra_sources;
    std::vector<std::filesystem::path> exclude_sources;
};

struct ModuleOverrides {
    std::vector<ExternalModuleProvider> external_providers;
};

struct ProjectProfile {
    BuildOverrides build;
    DiscoveryOverrides discovery;
    ModuleOverrides modules;
};

struct ProjectConfig {
    int version{1};
    std::filesystem::path file;
    std::filesystem::path project_root;
    BuildOverrides build;
    DiscoveryOverrides discovery;
    ModuleOverrides modules;
    std::map<std::string, ProjectProfile, std::less<>> profiles;
};

enum class ErrorCode {
    io_error,
    parse_error,
    schema_error,
    unsupported_version,
};

struct Error {
    ErrorCode code{ErrorCode::parse_error};
    std::filesystem::path path;
    std::size_t line{1};
    std::size_t column{1};
    std::string message;
};

class ProjectConfigLoader {
public:
    [[nodiscard]] static std::expected<std::optional<std::filesystem::path>, Error>
    find_upwards(const std::filesystem::path& start_directory);

    [[nodiscard]] static std::expected<ProjectConfig, Error>
    load(const std::filesystem::path& file);
};

} // namespace mqb::config
