#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"

#include <algorithm>
#include <array>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "mqb/core/BuildPlanner.hpp"
#include "mqb/core/CompileCache.hpp"
#include "mqb/core/CompileCacheFile.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"

#include "IncrementalFileSnapshot.hpp"

namespace mqb::orchestration {
namespace {

void append_snapshot_warning(
    std::vector<IncrementalCompileWarning>& warnings,
    const std::filesystem::path& path,
    std::optional<detail::IncrementalFileSnapshotFailure> failure) {
    if (!failure) {
        return;
    }

    std::string message;
    switch (failure->kind) {
    case detail::IncrementalFileSnapshotFailureKind::status:
        message = "failed to query file type while validating compile cache";
        break;
    case detail::IncrementalFileSnapshotFailureKind::timestamp:
        message = "failed to read file timestamp while validating compile cache";
        break;
    }
    warnings.push_back(IncrementalCompileWarning{
        .code = IncrementalCompileWarningCode::file_snapshot_failed,
        .path = path,
        .message = std::move(message),
    });
}

void add_reason_once(
    std::vector<BuildReason>& reasons,
    const BuildReason reason) {
    if (std::find(reasons.begin(), reasons.end(), reason) == reasons.end()) {
        reasons.push_back(reason);
    }
}

} // namespace

std::expected<IncrementalCompileResult, IncrementalCompileError>
MsvcIncrementalCompileCoordinator::run(const IncrementalCompileRequest& request) const {
    IncrementalCompileResult result;

    std::optional<CompileCacheEntry> cached_entry;
    auto loaded_cache = CompileCacheFile::load(request.cache_file);
    if (!loaded_cache) {
        result.warnings.push_back(IncrementalCompileWarning{
            .code = IncrementalCompileWarningCode::cache_load_failed,
            .path = request.cache_file,
            .message = loaded_cache.error().message,
        });
    } else {
        cached_entry = std::move(*loaded_cache);
    }

    auto source_snapshot = detail::snapshot_regular_file(request.unit.source);
    append_snapshot_warning(
        result.warnings,
        request.unit.source,
        std::move(source_snapshot.failure));

    std::vector<FileSnapshot> output_snapshots;
    output_snapshots.reserve(request.unit.outputs.size());
    for (const auto& output : request.unit.outputs) {
        auto output_snapshot = detail::snapshot_regular_file(output.path);
        append_snapshot_warning(
            result.warnings,
            output.path,
            std::move(output_snapshot.failure));
        output_snapshots.push_back(std::move(output_snapshot.snapshot));
    }

    std::vector<FileSnapshot> dependency_snapshots;
    if (cached_entry) {
        dependency_snapshots.reserve(cached_entry->dependencies.size());
        for (const auto& dependency : cached_entry->dependencies) {
            auto dependency_snapshot = detail::snapshot_regular_file(dependency);
            append_snapshot_warning(
                result.warnings,
                dependency,
                std::move(dependency_snapshot.failure));
            dependency_snapshots.push_back(std::move(dependency_snapshot.snapshot));
        }
    }

    result.validation = CompileCacheValidator::validate(
        request.unit,
        toolchain_.identity,
        request.options,
        cached_entry,
        source_snapshot.snapshot,
        output_snapshots,
        dependency_snapshots);

    if (request.force_rebuild) {
        add_reason_once(result.validation.reasons, BuildReason::explicit_rebuild);
    }

    // The validator is authoritative for incremental freshness. Once it says
    // this compile is reusable there is no action for the generic planner to
    // derive, so return directly on the hot no-op path. Cold/miss paths still
    // flow through BuildPlanner and retain all existing structural validation.
    if (result.validation.reusable()) {
        return result;
    }

    const std::array<CompilePlanItem, 1> items{
        CompilePlanItem{
            .unit = request.unit,
            .cache_validation = result.validation,
        },
    };
    auto planned = BuildPlanner::plan_compile(items);
    if (!planned) {
        return std::unexpected(IncrementalCompileError{
            .code = IncrementalCompileErrorCode::planning_failed,
            .message = "failed to create compile build plan",
            .planner_error = planned.error(),
        });
    }
    result.plan = std::move(*planned);

    if (result.plan.actions.empty()) {
        return result;
    }

    msvc::CompileExecutionRequest execution_request{
        .unit = request.unit,
        .options = request.options,
        .source_dependencies_file = request.source_dependencies_file,
        .working_directory = request.working_directory,
    };
    auto executed = executor_.execute(execution_request);
    if (!executed) {
        return std::unexpected(IncrementalCompileError{
            .code = IncrementalCompileErrorCode::compile_failed,
            .message = "planned MSVC compile action failed",
            .compile_error = executed.error(),
        });
    }

    result.compiled = true;
    result.process = std::move(executed->process);

    auto saved_cache = CompileCacheFile::save(
        request.cache_file,
        executed->cache_entry);
    if (!saved_cache) {
        result.warnings.push_back(IncrementalCompileWarning{
            .code = IncrementalCompileWarningCode::cache_save_failed,
            .path = request.cache_file,
            .message = saved_cache.error().message,
        });
    }

    return result;
}

} // namespace mqb::orchestration
