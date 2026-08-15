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
#include "mqb/msvc/MsvcParameterEngine.hpp"

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
    std::vector<IncrementalLinkWarning>& warnings) {
    snapshots.reserve(paths.size());
    for (const auto& path : paths) {
        auto snapshot = detail::snapshot_regular_file(path);
        if (snapshot.failure) {
            warnings.push_back(IncrementalLinkWarning{
                .code = IncrementalLinkWarningCode::file_snapshot_failed,
                .path = path,
                .message = snapshot_failure_message(*snapshot.failure),
            });
        }
        snapshots.push_back(std::move(snapshot.snapshot));
    }
}

void collect_existing_side_output(
    const fs::path& path,
    std::vector<fs::path>& outputs,
    std::vector<IncrementalLinkWarning>& warnings) {
    std::error_code error_code;
    const bool exists = fs::exists(path, error_code);
    if (error_code) {
        warnings.push_back(IncrementalLinkWarning{
            .code = IncrementalLinkWarningCode::file_snapshot_failed,
            .path = path,
            .message = "failed to query linker side output: " + error_code.message(),
        });
        return;
    }
    if (exists) {
        outputs.push_back(path);
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

    auto linker_file_routing = msvc::MsvcParameterEngine::linker_file_inputs(
        request.options.additional_arguments,
        request.working_directory);
    if (!linker_file_routing) {
        return std::unexpected(IncrementalLinkError{
            .code = IncrementalLinkErrorCode::linker_parameter_invalid,
            .message = "invalid tracked MSVC linker file input: "
                + linker_file_routing.error().message,
            .parameter_error = linker_file_routing.error(),
        });
    }
    std::vector<fs::path> linker_file_inputs;
    linker_file_inputs.reserve(linker_file_routing->inputs.size());
    for (const auto& input : linker_file_routing->inputs) {
        linker_file_inputs.push_back(input.path);
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

    auto output_snapshot_result = detail::snapshot_regular_file(request.output);
    if (output_snapshot_result.failure) {
        result.warnings.push_back(IncrementalLinkWarning{
            .code = IncrementalLinkWarningCode::file_snapshot_failed,
            .path = request.output,
            .message = snapshot_failure_message(*output_snapshot_result.failure),
        });
    }
    FileSnapshot output_snapshot = std::move(output_snapshot_result.snapshot);

    std::vector<FileSnapshot> object_snapshots;
    snapshot_inputs(request.objects, object_snapshots, result.warnings);

    std::vector<FileSnapshot> library_snapshots;
    snapshot_inputs(resolved_libraries->files, library_snapshots, result.warnings);

    std::vector<FileSnapshot> file_input_snapshots;
    snapshot_inputs(linker_file_inputs, file_input_snapshots, result.warnings);

    std::vector<FileSnapshot> side_output_snapshots;
    if (cached_entry) {
        snapshot_inputs(cached_entry->side_outputs, side_output_snapshots, result.warnings);
    }

    result.validation = LinkCacheValidator::validate(
        request.objects,
        resolved_libraries->files,
        linker_file_inputs,
        request.output,
        *linker_identity,
        request.options,
        cached_entry,
        output_snapshot,
        object_snapshots,
        library_snapshots,
        file_input_snapshots,
        side_output_snapshots,
        request.force_relink);

    // A reusable link validation is already the final no-op decision. Avoid
    // materializing a generic LinkPlanItem/BuildPlan on the hot path; misses
    // still pass through BuildPlanner and keep its structural checks.
    if (result.validation.reusable()) {
        return result;
    }

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
        .force_full_link = result.validation.library_inputs_changed
            || result.validation.file_inputs_changed
            || linker_file_routing->requires_full_link,
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

    std::vector<fs::path> side_outputs;
    if (request.options.target_kind == TargetKind::dynamic_library) {
        collect_existing_side_output(
            msvc::MsvcLinker::import_library_path(action->output),
            side_outputs,
            result.warnings);
        collect_existing_side_output(
            msvc::MsvcLinker::export_file_path(action->output),
            side_outputs,
            result.warnings);
    }

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
        .file_inputs = linker_file_inputs,
        .side_outputs = std::move(side_outputs),
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
