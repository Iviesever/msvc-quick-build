#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"

#include <array>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "mqb/core/Artifact.hpp"
#include "mqb/core/BuildPlanner.hpp"
#include "mqb/core/CompileCache.hpp"
#include "mqb/core/CompileCacheFile.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"

namespace mqb::orchestration {
namespace {

namespace fs = std::filesystem;

struct SnapshotResult {
    FileSnapshot snapshot;
    std::optional<IncrementalCompileWarning> warning;
};

[[nodiscard]] SnapshotResult missing_snapshot(const fs::path& path) {
    return SnapshotResult{
        .snapshot = FileSnapshot{
            .path = path,
            .exists = false,
        },
    };
}

[[nodiscard]] SnapshotResult snapshot_file(const fs::path& path) {
    if (path.empty()) {
        return missing_snapshot(path);
    }

    std::error_code error_code;
    const fs::file_status status = fs::status(path, error_code);
    if (error_code) {
        if (error_code == std::errc::no_such_file_or_directory) {
            return missing_snapshot(path);
        }
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
    if (!fs::is_regular_file(status)) {
        return missing_snapshot(path);
    }

    const auto modified = fs::last_write_time(path, error_code);
    if (error_code) {
        if (error_code == std::errc::no_such_file_or_directory) {
            return missing_snapshot(path);
        }
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

[[nodiscard]] fs::path first_object_path(const TranslationUnit& unit) {
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

    auto source_snapshot = snapshot_file(request.unit.source);
    append_warning(result.warnings, std::move(source_snapshot.warning));

    auto object_snapshot = snapshot_file(first_object_path(request.unit));
    append_warning(result.warnings, std::move(object_snapshot.warning));

    std::vector<FileSnapshot> dependency_snapshots;
    if (cached_entry) {
        dependency_snapshots.reserve(cached_entry->dependencies.size());
        for (const auto& dependency : cached_entry->dependencies) {
            auto dependency_snapshot = snapshot_file(dependency);
            append_warning(result.warnings, std::move(dependency_snapshot.warning));
            dependency_snapshots.push_back(std::move(dependency_snapshot.snapshot));
        }
    }

    result.validation = CompileCacheValidator::validate(
        request.unit,
        toolchain_.identity,
        request.options,
        cached_entry,
        source_snapshot.snapshot,
        object_snapshot.snapshot,
        dependency_snapshots);

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
