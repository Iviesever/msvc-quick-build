#include "mqb/application/IncrementalCompileCoordinator.hpp"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "mqb/core/Artifact.hpp"
#include "mqb/core/BuildPlanner.hpp"
#include "mqb/core/CompileCache.hpp"
#include "mqb/core/CompileCacheFile.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"

namespace mqb::application {
namespace {

namespace fs = std::filesystem;

struct SnapshotResult {
    FileSnapshot snapshot;
    std::optional<IncrementalCompileWarning> warning;
};

[[nodiscard]] SnapshotResult snapshot_file(const fs::path& path) {
    if (path.empty()) {
        return SnapshotResult{
            .snapshot = FileSnapshot{
                .path = path,
                .exists = false,
            },
        };
    }

    std::error_code error_code;
    const bool exists = fs::is_regular_file(path, error_code);
    if (error_code) {
        return SnapshotResult{
            .snapshot = FileSnapshot{
                .path = path,
                .exists = false,
            },
            .warning = IncrementalCompileWarning{
                .code = IncrementalCompileWarningCode::file_snapshot_failed,
                .path = path,
                .message = "failed to query file type while validating compile cache",
            },
        };
    }
    if (!exists) {
        return SnapshotResult{
            .snapshot = FileSnapshot{
                .path = path,
                .exists = false,
            },
        };
    }

    const auto modified = fs::last_write_time(path, error_code);
    if (error_code) {
        return SnapshotResult{
            .snapshot = FileSnapshot{
                .path = path,
                .exists = false,
            },
            .warning = IncrementalCompileWarning{
                .code = IncrementalCompileWarningCode::file_snapshot_failed,
                .path = path,
                .message = "failed to read file timestamp while validating compile cache",
            },
        };
    }

    return SnapshotResult{
        .snapshot = FileSnapshot{
            .path = path,
            .exists = true,
            .modified = modified,
        },
    };
}

[[nodiscard]] fs::path planned_object_path(const TranslationUnit& unit) {
    for (const auto& output : unit.outputs) {
        if (output.kind == ArtifactKind::object) {
            return output.path;
        }
    }
    return {};
}

void append_warning(
    std::vector<IncrementalCompileWarning>& warnings,
    std::optional<IncrementalCompileWarning> warning) {
    if (warning) {
        warnings.push_back(std::move(*warning));
    }
}

} // namespace

std::expected<IncrementalCompileResult, IncrementalCompileError>
IncrementalCompileCoordinator::run(const IncrementalCompileRequest& request) const {
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

    const auto source_snapshot = snapshot_file(request.unit.source);
    append_warning(result.warnings, source_snapshot.warning);

    const auto object_snapshot = snapshot_file(planned_object_path(request.unit));
    append_warning(result.warnings, object_snapshot.warning);

    std::vector<FileSnapshot> dependency_snapshots;
    if (cached_entry) {
        dependency_snapshots.reserve(cached_entry->dependencies.size());
        for (const auto& dependency : cached_entry->dependencies) {
            auto snapshot = snapshot_file(dependency);
            append_warning(result.warnings, snapshot.warning);
            dependency_snapshots.push_back(std::move(snapshot.snapshot));
        }
    }

    result.validation = CompileCacheValidator::validate(
        request.unit,
        executor_.toolchain_identity(),
        request.options,
        cached_entry,
        source_snapshot.snapshot,
        object_snapshot.snapshot,
        dependency_snapshots);

    const CompilePlanItem item{
        .unit = request.unit,
        .cache_validation = result.validation,
    };
    const std::array<CompilePlanItem, 1> items{item};
    auto plan = BuildPlanner::plan_compile(items);
    if (!plan) {
        return std::unexpected(IncrementalCompileError{
            .code = IncrementalCompileErrorCode::planning_failed,
            .message = "failed to create compile build plan",
            .planner_error = plan.error(),
        });
    }
    result.plan = std::move(*plan);

    if (result.plan.empty()) {
        return result;
    }

    msvc::CompileExecutionRequest execution_request{
        .unit = request.unit,
        .options = request.options,
        .source_dependencies_file = request.source_dependencies_file,
        .working_directory = request.working_directory,
    };
    auto execution = executor_.execute(execution_request);
    if (!execution) {
        return std::unexpected(IncrementalCompileError{
            .code = IncrementalCompileErrorCode::compile_failed,
            .message = "planned MSVC compile action failed",
            .compile_error = execution.error(),
        });
    }

    result.compiled = true;
    result.process = std::move(execution->process);

    auto saved_cache = CompileCacheFile::save(
        request.cache_file,
        execution->cache_entry);
    if (!saved_cache) {
        result.warnings.push_back(IncrementalCompileWarning{
            .code = IncrementalCompileWarningCode::cache_save_failed,
            .path = request.cache_file,
            .message = saved_cache.error().message,
        });
    }

    return result;
}

} // namespace mqb::application
