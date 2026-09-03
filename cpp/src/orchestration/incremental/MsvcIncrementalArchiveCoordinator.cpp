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

struct ArchiveInspectionState {
    IncrementalArchiveInspection inspection;
    LibrarianIdentity librarian_identity;
    std::optional<msvc::ArchiveInvocation> invocation;
};

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

[[nodiscard]] std::expected<ArchiveInspectionState, IncrementalArchiveError>
inspect_archive(
    const IncrementalArchiveRequest& request,
    const msvc::MsvcToolchain& toolchain) {
    ArchiveInspectionState state;

    auto librarian_identity = msvc::MsvcLibrarian::identity(toolchain);
    if (!librarian_identity) {
        return std::unexpected(IncrementalArchiveError{
            .code = IncrementalArchiveErrorCode::librarian_identity_failed,
            .message = "failed to identify MSVC librarian",
            .librarian_error = librarian_identity.error(),
        });
    }
    state.librarian_identity = std::move(*librarian_identity);

    auto librarian_routing = msvc::MsvcParameterEngine::route_librarian(
        request.additional_arguments);
    if (!librarian_routing) {
        return std::unexpected(IncrementalArchiveError{
            .code = IncrementalArchiveErrorCode::librarian_parameter_invalid,
            .message = "invalid native MSVC librarian argument: "
                + librarian_routing.error().message,
            .parameter_error = librarian_routing.error(),
        });
    }
    if (librarian_routing->architecture
        && *librarian_routing->architecture != request.architecture) {
        return std::unexpected(IncrementalArchiveError{
            .code = IncrementalArchiveErrorCode::librarian_parameter_invalid,
            .message = "native MSVC librarian /MACHINE conflicts with the typed target architecture",
        });
    }
    const bool effective_ltcg = request.link_time_code_generation
        || librarian_routing->link_time_code_generation.value_or(false);

    std::optional<ArchiveCacheEntry> cached_entry;
    auto loaded = ArchiveCacheFile::load(request.cache_file);
    if (loaded) {
        cached_entry = std::move(*loaded);
    } else {
        state.inspection.warnings.push_back(IncrementalArchiveWarning{
            .code = IncrementalArchiveWarningCode::cache_load_failed,
            .path = request.cache_file,
            .message = loaded.error().message,
        });
    }

    auto output_snapshot_result = detail::snapshot_regular_file(request.output);
    if (output_snapshot_result.failure) {
        state.inspection.warnings.push_back(IncrementalArchiveWarning{
            .code = IncrementalArchiveWarningCode::file_snapshot_failed,
            .path = request.output,
            .message = snapshot_failure_message(*output_snapshot_result.failure),
        });
    }
    FileSnapshot output_snapshot = std::move(output_snapshot_result.snapshot);

    std::vector<FileSnapshot> object_snapshots;
    snapshot_inputs(request.objects, object_snapshots, state.inspection.warnings);

    state.inspection.validation = ArchiveCacheValidator::validate(
        request.objects,
        request.output,
        state.librarian_identity,
        cached_entry,
        output_snapshot,
        object_snapshots,
        request.force_archive,
        effective_ltcg,
        request.architecture,
        librarian_routing->passthrough);

    // A reusable archive validation already proves that there is no archive
    // action to schedule. Keep the generic planner on miss/rebuild paths only.
    if (state.inspection.validation.reusable()) {
        return state;
    }

    auto plan = BuildPlanner::plan_archive(ArchivePlanItem{
        .objects = request.objects,
        .output = request.output,
        .cache_validation = state.inspection.validation,
    });
    if (!plan) {
        return std::unexpected(IncrementalArchiveError{
            .code = IncrementalArchiveErrorCode::planning_failed,
            .message = "failed to create static archive build plan",
        });
    }
    state.inspection.plan = std::move(*plan);
    if (state.inspection.plan.empty()) return state;
    if (state.inspection.plan.actions.size() != 1) {
        return std::unexpected(IncrementalArchiveError{
            .code = IncrementalArchiveErrorCode::planning_failed,
            .message = "single-target archive coordinator expected exactly one build action",
        });
    }

    const auto* action = std::get_if<ArchiveAction>(&state.inspection.plan.actions.front());
    if (action == nullptr) {
        return std::unexpected(IncrementalArchiveError{
            .code = IncrementalArchiveErrorCode::planning_failed,
            .message = "archive coordinator received a non-archive build action",
        });
    }

    state.invocation = msvc::ArchiveInvocation{
        .objects = action->objects,
        .output = action->output,
        .working_directory = request.working_directory,
        .architecture = request.architecture,
        .link_time_code_generation = effective_ltcg,
        .additional_arguments = librarian_routing->passthrough,
    };
    return state;
}

} // namespace

std::expected<IncrementalArchiveInspection, IncrementalArchiveError>
MsvcIncrementalArchiveCoordinator::inspect(const IncrementalArchiveRequest& request) const {
    auto state = inspect_archive(request, toolchain_);
    if (!state) return std::unexpected(state.error());
    return std::move(state->inspection);
}

std::expected<IncrementalArchiveResult, IncrementalArchiveError>
MsvcIncrementalArchiveCoordinator::run(const IncrementalArchiveRequest& request) const {
    auto inspected = inspect_archive(request, toolchain_);
    if (!inspected) return std::unexpected(inspected.error());

    IncrementalArchiveResult result;
    result.validation = std::move(inspected->inspection.validation);
    result.plan = std::move(inspected->inspection.plan);
    result.warnings = std::move(inspected->inspection.warnings);

    if (!inspected->invocation) {
        return result;
    }

    auto archived = librarian_.archive(*inspected->invocation);
    if (!archived) {
        return std::unexpected(IncrementalArchiveError{
            .code = IncrementalArchiveErrorCode::archive_failed,
            .message = "MSVC archive action failed",
            .librarian_error = archived.error(),
        });
    }
    result.archived = true;
    result.process = std::move(*archived);

    const auto& invocation = *inspected->invocation;
    const ArchiveCacheEntry entry{
        .librarian = inspected->librarian_identity,
        .signature = BuildSignature::for_archive(
            invocation.objects,
            invocation.output,
            inspected->librarian_identity,
            invocation.link_time_code_generation,
            invocation.architecture,
            invocation.additional_arguments),
        .objects = invocation.objects,
        .output = invocation.output,
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
