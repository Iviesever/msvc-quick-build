#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mqb/core/ArchiveCache.hpp"
#include "mqb/core/BuildPlan.hpp"
#include "mqb/msvc/MsvcLibrarian.hpp"
#include "mqb/msvc/MsvcParameterEngine.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/process/Process.hpp"

namespace mqb::orchestration {

struct IncrementalArchiveRequest {
    std::vector<std::filesystem::path> objects;
    std::filesystem::path output;
    std::filesystem::path cache_file;
    std::filesystem::path working_directory;
    Architecture architecture{Architecture::x64};
    bool link_time_code_generation{false};
    std::vector<std::string> additional_arguments;
    bool force_archive{false};
};

enum class IncrementalArchiveWarningCode {
    cache_load_failed,
    cache_save_failed,
    file_snapshot_failed,
};

struct IncrementalArchiveWarning {
    IncrementalArchiveWarningCode code{IncrementalArchiveWarningCode::file_snapshot_failed};
    std::filesystem::path path;
    std::string message;
};

enum class IncrementalArchiveErrorCode {
    librarian_identity_failed,
    librarian_parameter_invalid,
    planning_failed,
    archive_failed,
};

struct IncrementalArchiveError {
    IncrementalArchiveErrorCode code{IncrementalArchiveErrorCode::planning_failed};
    std::string message;
    std::optional<msvc::ParameterError> parameter_error;
    std::optional<msvc::LibrarianError> librarian_error;
};

struct IncrementalArchiveInspection {
    ArchiveCacheValidation validation;
    BuildPlan plan;
    std::vector<IncrementalArchiveWarning> warnings;
};

struct IncrementalArchiveResult : IncrementalArchiveInspection {
    bool archived{false};
    std::optional<process::ProcessResult> process;
};

class MsvcIncrementalArchiveCoordinator {
public:
    MsvcIncrementalArchiveCoordinator(
        const msvc::MsvcToolchain& toolchain,
        msvc::MsvcLibrarian& librarian)
        : toolchain_(toolchain), librarian_(librarian) {}

    // Read librarian/cache freshness and derive the exact BuildPlan without
    // launching lib.exe or mutating archive cache/output state.
    [[nodiscard]] std::expected<IncrementalArchiveInspection, IncrementalArchiveError>
    inspect(const IncrementalArchiveRequest& request) const;

    [[nodiscard]] std::expected<IncrementalArchiveResult, IncrementalArchiveError>
    run(const IncrementalArchiveRequest& request) const;

private:
    const msvc::MsvcToolchain& toolchain_;
    msvc::MsvcLibrarian& librarian_;
};

} // namespace mqb::orchestration
