#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mqb/core/ArchiveCache.hpp"
#include "mqb/core/BuildPlan.hpp"
#include "mqb/msvc/MsvcLibrarian.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/process/Process.hpp"

namespace mqb::orchestration {

struct IncrementalArchiveRequest {
    std::vector<std::filesystem::path> objects;
    std::filesystem::path output;
    std::filesystem::path cache_file;
    std::filesystem::path working_directory;
    bool link_time_code_generation{false};
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
    planning_failed,
    archive_failed,
};

struct IncrementalArchiveError {
    IncrementalArchiveErrorCode code{IncrementalArchiveErrorCode::planning_failed};
    std::string message;
    std::optional<msvc::LibrarianError> librarian_error;
};

struct IncrementalArchiveResult {
    ArchiveCacheValidation validation;
    BuildPlan plan;
    bool archived{false};
    std::optional<process::ProcessResult> process;
    std::vector<IncrementalArchiveWarning> warnings;
};

class MsvcIncrementalArchiveCoordinator {
public:
    MsvcIncrementalArchiveCoordinator(
        const msvc::MsvcToolchain& toolchain,
        msvc::MsvcLibrarian& librarian)
        : toolchain_(toolchain), librarian_(librarian) {}

    [[nodiscard]] std::expected<IncrementalArchiveResult, IncrementalArchiveError>
    run(const IncrementalArchiveRequest& request) const;

private:
    const msvc::MsvcToolchain& toolchain_;
    msvc::MsvcLibrarian& librarian_;
};

} // namespace mqb::orchestration
