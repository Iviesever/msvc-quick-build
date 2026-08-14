#include "mqb/orchestration/MsvcIncrementalArchiveCoordinator.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "mqb/core/ArchiveCacheFile.hpp"
#include "mqb/core/BuildAction.hpp"
#include "mqb/core/BuildPlanner.hpp"
#include "mqb/core/BuildSignature.hpp"
#include "mqb/core/FileSnapshot.hpp"

#include "IncrementalFileSnapshot.hpp"

namespace mqb::orchestration {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string snapshot_failure_message(
    const detail::IncrementalFileSnapshotFailure& failure) {
    const char* prefix = nullptr;
    switch (failure.kind) {
    case detail::IncrementalFileSnapshotFailureKind::status:
        prefix = "failed to query file type: ";
        break;
    case detail::IncrementalFileSnapshotFailureKind::timestamp:
        prefix = "failed to query file timestamp: ";
        break;
    }
    return std::string{prefix} + failure.error_code.message();
}

void snapshot_inputs(
    const std::vector<fs::path>& paths,
    std::vector<FileSnapshot>& snapshots,
    std::vector<IncrementalArchiveWarning>& warnings) {
    snapshots.reserve(paths.size());
    for (const auto& path : paths) {
        auto snapshot = detail::snapshot_regular_file(path);
        if (snapshot.failure) {
            warnings.push_back(IncrementalArchiveWarning{
                .code = IncrementalArchiveWarningCode::file_snapshot_failed,
                .path = path,
                .message = snapshot_failure_message(*snapshot.failure),
            });
        }
        snapshots.push_back(std::move(snapshot.snapshot));
    }
}

} // namespace

std::expected<IncrementalArchiveResult, IncrementalArchiveError>
MsvcIncrementalArchiveCoordinator::run(const IncrementalArchiveRequest& request) const {
    IncrementalArchiveResult result;

    auto librarian_identity = msvc::MsvcLibrarian::identity(toolchain_);
    if (!librarian_identity) {
        return std::unexpected(IncrementalArchiveError{
            .code = IncrementalArchiveErrorCode::librarian_identity_failed,
            .message = "failed to identify MSVC librarian",
            .librarian_error = librarian_identity.error(),
        });
    }

    std::optional<ArchiveCacheEntry> cached_entry;
    auto loaded = ArchiveCacheFile::load(request.cache_file);
    if (loaded) {
        cached_entry = std::move(*loaded);
    } else {
        result.warnings.push_back(IncrementalArchiveWarning{
            .code = IncrementalArchiveWarningCode::cache_load_failed,
            .path = request.cache_file,
            .message = loaded.error().message,
        });
    }

    auto output_snapshot_result = detail::snapshot_regular_file(request.output);
    if (output_snapshot_result.failure) {
        result.warnings.push_back(IncrementalArchiveWarning{
            .code = IncrementalArchiveWarningCode::file_snapshot_failed,
            .path = request.output,
            .message = snapshot_failure_message(*output_snapshot_result.failure),
        });
    }
    FileSnapshot output_snapshot = std::move(output_snapshot_result.snapshot);

    std::vector<FileSnapshot> object_snapshots;
    snapshot_inputs(request.objects, object_snapshots, result.warnings);

    result.validation = ArchiveCacheValidator::validate(
        request.objects,
        request.output,
        *librarian_identity,
        cached_entry,
        output_snapshot,
        object_snapshots,
        request.force_archive,
        request.link_time_code_generation);

    // A reusable archive validation already proves that there is no archive
    // action to schedule. Keep the generic planner on miss/rebuild paths only.
    if (result.validation.reusable()) {
        return result;
    }

    auto plan = BuildPlanner::plan_archive(ArchivePlanItem{
        .objects = request.objects,
        .output = request.output,
        .cache_validation = result.validation,
    });
    if (!plan) {
        return std::unexpected(IncrementalArchiveError{
            .code = IncrementalArchiveErrorCode::planning_failed,
            .message = "failed to create static archive build plan",
        });
    }
    result.plan = std::move(*plan);
    if (result.plan.empty()) return result;
    if (result.plan.actions.size() != 1) {
        return std::unexpected(IncrementalArchiveError{
            .code = IncrementalArchiveErrorCode::planning_failed,
            .message = "single-target archive coordinator expected exactly one build action",
        });
    }

    const auto* action = std::get_if<ArchiveAction>(&result.plan.actions.front());
    if (action == nullptr) {
        return std::unexpected(IncrementalArchiveError{
            .code = IncrementalArchiveErrorCode::planning_failed,
            .message = "archive coordinator received a non-archive build action",
        });
    }

    auto archived = librarian_.archive(msvc::ArchiveInvocation{
        .objects = action->objects,
        .output = action->output,
        .working_directory = request.working_directory,
        .link_time_code_generation = request.link_time_code_generation,
    });
    if (!archived) {
        return std::unexpected(IncrementalArchiveError{
            .code = IncrementalArchiveErrorCode::archive_failed,
            .message = "MSVC archive action failed",
            .librarian_error = archived.error(),
        });
    }
    result.archived = true;
    result.process = std::move(*archived);

    const ArchiveCacheEntry entry{
        .librarian = *librarian_identity,
        .signature = BuildSignature::for_archive(
            action->objects,
            action->output,
            *librarian_identity,
            request.link_time_code_generation),
        .objects = action->objects,
        .output = action->output,
    };
    auto saved = ArchiveCacheFile::save(request.cache_file, entry);
    if (!saved) {
        result.warnings.push_back(IncrementalArchiveWarning{
            .code = IncrementalArchiveWarningCode::cache_save_failed,
            .path = request.cache_file,
            .message = saved.error().message,
        });
    }
    return result;
}

} // namespace mqb::orchestration
