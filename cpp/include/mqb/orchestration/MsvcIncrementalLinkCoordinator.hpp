#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mqb/core/BuildPlan.hpp"
#include "mqb/core/BuildPlanner.hpp"
#include "mqb/core/LinkCache.hpp"
#include "mqb/core/LinkCacheFile.hpp"
#include "mqb/core/LinkOptions.hpp"
#include "mqb/msvc/MsvcLibraryResolver.hpp"
#include "mqb/msvc/MsvcLinker.hpp"
#include "mqb/msvc/MsvcParameterEngine.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/process/Process.hpp"

namespace mqb::orchestration {

struct IncrementalLinkRequest {
    std::vector<std::filesystem::path> objects;
    std::filesystem::path output;
    LinkOptions options;
    std::filesystem::path cache_file;
    std::optional<std::filesystem::path> working_directory;
    bool force_relink{false};
};

enum class IncrementalLinkWarningCode {
    cache_load_failed,
    cache_save_failed,
    file_snapshot_failed,
};

struct IncrementalLinkWarning {
    IncrementalLinkWarningCode code{IncrementalLinkWarningCode::file_snapshot_failed};
    std::filesystem::path path;
    std::string message;
};

enum class IncrementalLinkErrorCode {
    linker_identity_failed,
    linker_parameter_invalid,
    library_resolution_failed,
    planning_failed,
    link_failed,
};

struct IncrementalLinkError {
    IncrementalLinkErrorCode code{IncrementalLinkErrorCode::planning_failed};
    std::string message;
    std::optional<BuildPlannerError> planner_error;
    std::optional<msvc::ParameterError> parameter_error;
    std::optional<msvc::LibraryResolutionError> library_resolution_error;
    std::optional<msvc::LinkerError> linker_error;
};

struct IncrementalLinkInspection {
    LinkCacheValidation validation;
    BuildPlan plan;
    std::vector<IncrementalLinkWarning> warnings;
};

struct IncrementalLinkResult : IncrementalLinkInspection {
    bool linked{false};
    std::optional<process::ProcessResult> process;
};

class MsvcIncrementalLinkCoordinator {
public:
    MsvcIncrementalLinkCoordinator(
        const msvc::MsvcToolchain& toolchain,
        msvc::MsvcLinker& linker)
        : toolchain_(toolchain), linker_(linker) {}

    // Resolve linker/library freshness and derive the exact BuildPlan without
    // launching link.exe or mutating link cache/output state.
    [[nodiscard]] std::expected<IncrementalLinkInspection, IncrementalLinkError>
    inspect(const IncrementalLinkRequest& request) const;

    [[nodiscard]] std::expected<IncrementalLinkResult, IncrementalLinkError>
    run(const IncrementalLinkRequest& request) const;

private:
    const msvc::MsvcToolchain& toolchain_;
    msvc::MsvcLinker& linker_;
};

} // namespace mqb::orchestration
