#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "mqb/core/BuildAction.hpp"
#include "mqb/core/BuildSignature.hpp"
#include "mqb/core/FileSnapshot.hpp"
#include "mqb/core/LinkCache.hpp"
#include "mqb/core/LinkCacheFile.hpp"
#include "mqb/msvc/MsvcLibraryResolver.hpp"
#include "mqb/msvc/MsvcLinker.hpp"

namespace mqb::orchestration {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::expected<FileSnapshot, std::string>
snapshot_file(const fs::path& path) {
    std::error_code error_code;
    const bool exists = fs::exists(path, error_code);
    if (error_code) {
        return std::unexpected("failed to query file existence: " + error_code.message());
    }
    if (!exists) {
        return FileSnapshot{.path = path, .exists = false};
    }

    const auto modified = fs::last_write_time(path, error_code);
    if (error_code) {
        return std::unexpected("failed to query file timestamp: " + error_code.message());
    }
    return FileSnapshot{
        .path = path,
        .exists = true,
        .modified = modified,
    };
}

void snapshot_inputs(
    const std::vector<fs::path>& paths,
    std::vector<FileSnapshot>& snapshots,
    std::vector<IncrementalLinkWarning>& warnings) {
    snapshots.reserve(paths.size());
    for (const auto& path : paths) {
        if (auto snapshot = snapshot_file(path)) {
            snapshots.push_back(std::move(*snapshot));
        } else {
            warnings.push_back(IncrementalLinkWarning{
                .code = IncrementalLinkWarningCode::file_snapshot_failed,
                .path = path,
                .message = snapshot.error(),
            });
            snapshots.push_back(FileSnapshot{.path = path, .exists = false});
        }
    }
}

[[nodiscard]] IncrementalLinkError link_failure(
    const IncrementalLinkErrorCode code,
    std::string message,
    std::optional<msvc::LinkerError> linker_error = std::nullopt) {
    return IncrementalLinkError{
        .code = code,
        .message = std::move(message),
        .linker_error = std::move(linker_error),
    };
}

} // namespace

std::expected<IncrementalLinkResult, IncrementalLinkError>
MsvcIncrementalLinkCoordinator::run(const IncrementalLinkRequest& request) const {
    IncrementalLinkResult result;

    auto linker_identity = msvc::MsvcLinker::identity(toolchain_);
    if (!linker_identity) {
        return std::unexpected(link_failure(
            IncrementalLinkErrorCode::linker_identity_failed,
            "failed to identify MSVC linker",
            linker_identity.error()));
    }

    auto resolved_libraries = msvc::MsvcLibraryResolver::resolve(
        toolchain_,
        request.options,
        request.working_directory.value_or(fs::path{}));
    if (!resolved_libraries) {
        IncrementalLinkError error{
            .code = IncrementalLinkErrorCode::library_resolution_failed,
            .message = "failed to resolve requested MSVC library: "
                + resolved_libraries.error().message,
            .library_resolution_error = resolved_libraries.error(),
        };
        return std::unexpected(std::move(error));
    }

    std::optional<LinkCacheEntry> cached_entry;
    auto loaded = LinkCacheFile::load(request.cache_file);
    if (loaded) {
        cached_entry = std::move(*loaded);
    } else {
        result.warnings.push_back(IncrementalLinkWarning{
            .code = IncrementalLinkWarningCode::cache_load_failed,
            .path = request.cache_file,
            .message = loaded.error().message,
        });
    }

    FileSnapshot output_snapshot{.path = request.output, .exists = false};
    if (auto snapshot = snapshot_file(request.output)) {
        output_snapshot = std::move(*snapshot);
    } else {
        result.warnings.push_back(IncrementalLinkWarning{
            .code = IncrementalLinkWarningCode::file_snapshot_failed,
            .path = request.output,
            .message = snapshot.error(),
        });
    }

    std::vector<FileSnapshot> object_snapshots;
    snapshot_inputs(request.objects, object_snapshots, result.warnings);

    std::vector<FileSnapshot> library_snapshots;
    snapshot_inputs(resolved_libraries->files, library_snapshots, result.warnings);

    result.validation = LinkCacheValidator::validate(
        request.objects,
        resolved_libraries->files,
        request.output,
        *linker_identity,
        request.options,
        cached_entry,
        output_snapshot,
        object_snapshots,
        library_snapshots,
        request.force_relink);

    const LinkPlanItem plan_item{
        .objects = request.objects,
        .output = request.output,
        .libraries = resolved_libraries->files,
        .cache_validation = result.validation,
    };
    auto plan = BuildPlanner::plan_link(plan_item);
    if (!plan) {
        return std::unexpected(IncrementalLinkError{
            .code = IncrementalLinkErrorCode::planning_failed,
            .message = "failed to create link build plan",
            .planner_error = plan.error(),
        });
    }
    result.plan = std::move(*plan);
    if (result.plan.empty()) {
        return result;
    }

    if (result.plan.actions.size() != 1) {
        return std::unexpected(IncrementalLinkError{
            .code = IncrementalLinkErrorCode::planning_failed,
            .message = "single-target link coordinator expected exactly one build action",
        });
    }

    const auto* action = std::get_if<LinkAction>(&result.plan.actions.front());
    if (action == nullptr) {
        return std::unexpected(IncrementalLinkError{
            .code = IncrementalLinkErrorCode::planning_failed,
            .message = "single-target link coordinator received a non-link build action",
        });
    }

    msvc::LinkInvocation invocation{
        .objects = action->objects,
        .output = action->output,
        .libraries = action->libraries,
        .options = request.options,
        .working_directory = request.working_directory.value_or(fs::path{}),
    };
    auto linked = linker_.link(invocation);
    if (!linked) {
        return std::unexpected(link_failure(
            IncrementalLinkErrorCode::link_failed,
            "MSVC link action failed",
            linked.error()));
    }

    result.linked = true;
    result.process = std::move(*linked);

    const LinkCacheEntry new_entry{
        .linker = *linker_identity,
        .signature = BuildSignature::for_link(
            action->objects,
            action->libraries,
            action->output,
            *linker_identity,
            request.options),
        .objects = action->objects,
        .output = action->output,
        .libraries = action->libraries,
    };
    auto saved = LinkCacheFile::save(request.cache_file, new_entry);
    if (!saved) {
        result.warnings.push_back(IncrementalLinkWarning{
            .code = IncrementalLinkWarningCode::cache_save_failed,
            .path = request.cache_file,
            .message = saved.error().message,
        });
    }

    return result;
}

} // namespace mqb::orchestration
