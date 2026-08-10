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
    object_snapshots.reserve(request.objects.size());
    for (const auto& object : request.objects) {
        if (auto snapshot = snapshot_file(object)) {
            object_snapshots.push_back(std::move(*snapshot));
        } else {
            result.warnings.push_back(IncrementalLinkWarning{
                .code = IncrementalLinkWarningCode::file_snapshot_failed,
                .path = object,
                .message = snapshot.error(),
            });
            object_snapshots.push_back(FileSnapshot{.path = object, .exists = false});
        }
    }

    result.validation = LinkCacheValidator::validate(
        request.objects,
        request.output,
        *linker_identity,
        request.options,
        cached_entry,
        output_snapshot,
        object_snapshots,
        request.force_relink);

    const LinkPlanItem plan_item{
        .objects = request.objects,
        .output = request.output,
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
            action->output,
            *linker_identity,
            request.options),
        .objects = action->objects,
        .output = action->output,
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
