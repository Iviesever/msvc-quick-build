#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"

#include <algorithm>
#include <array>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "mqb/core/BuildPlanner.hpp"
#include "mqb/core/CompileCache.hpp"
#include "mqb/core/CompileCacheFile.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcIncludeSearchFreshness.hpp"
#include "mqb/msvc/MsvcSourceDependenciesReader.hpp"
#include "mqb/platform/windows/PathIdentity.hpp"

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

[[nodiscard]] bool same_path(
    const std::filesystem::path& left,
    const std::filesystem::path& right) {
    return mqb::platform::windows::path_identity_key(left)
        == mqb::platform::windows::path_identity_key(right);
}

[[nodiscard]] bool same_ordered_paths(
    const std::span<const std::filesystem::path> left,
    const std::span<const std::filesystem::path> right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!same_path(left[index], right[index])) return false;
    }
    return true;
}

void add_dependency_once(
    std::vector<std::filesystem::path>& dependencies,
    const std::filesystem::path& dependency) {
    if (dependency.empty()) return;
    if (std::none_of(dependencies.begin(), dependencies.end(), [&](const auto& existing) {
            return same_path(existing, dependency);
        })) {
        dependencies.push_back(dependency.lexically_normal());
    }
}

void seal_module_scan_evidence(
    CompileCacheEntry& cache_entry,
    const IncrementalCompileRequest& request,
    const msvc::MsvcToolchain& toolchain,
    std::vector<IncrementalCompileWarning>& warnings) {
    if (!request.module_scan_output) {
        return;
    }

    // Re-read the compiler's successful /sourceDependencies output instead of
    // using cache_entry.dependencies: the latter also contains generated IFC
    // and PCH inputs which can legitimately be newer than the P1689 scan.
    const auto dependencies = msvc::MsvcSourceDependenciesReader::read(
        request.source_dependencies_file);
    if (!dependencies) {
        return;
    }

    auto source = detail::snapshot_regular_file(request.unit.source);
    append_snapshot_warning(warnings, request.unit.source, std::move(source.failure));
    auto output = detail::snapshot_regular_file(*request.module_scan_output);
    append_snapshot_warning(warnings, *request.module_scan_output, std::move(output.failure));
    if (!source.snapshot.exists || !output.snapshot.exists) {
        return;
    }

    std::vector<std::filesystem::path> scan_dependencies = dependencies->includes;
    const auto include_freshness = msvc::include_search_freshness_directories(
        dependencies->includes,
        request.unit.source,
        request.options,
        toolchain.environment,
        request.working_directory);
    for (const auto& directory : include_freshness) {
        add_dependency_once(scan_dependencies, directory);
    }

    std::vector<FileSnapshot> include_snapshots;
    include_snapshots.reserve(scan_dependencies.size());
    for (const auto& dependency : scan_dependencies) {
        auto snapshot = detail::snapshot_file_or_directory(dependency);
        append_snapshot_warning(warnings, dependency, std::move(snapshot.failure));
        if (!snapshot.snapshot.exists) {
            return;
        }
        include_snapshots.push_back(std::move(snapshot.snapshot));
    }

    // A source/header/search-root mutation after /scanDependencies but before
    // the compile completed means the topology artifact is not a trustworthy
    // description of the successful compile. In that case keep the normal
    // compile cache but deliberately omit scan evidence so the next build rescans.
    if (source.snapshot.modified > output.snapshot.modified) {
        return;
    }
    for (const auto& dependency : include_snapshots) {
        if (dependency.modified > output.snapshot.modified) {
            return;
        }
    }

    cache_entry.module_scan = ModuleScanEvidence{
        .signature = BuildSignature::for_module_scan(
            request.unit.source,
            request.unit.kind,
            toolchain.identity,
            request.options),
        .source = std::move(source.snapshot),
        .output = std::move(output.snapshot),
        .dependencies = std::move(include_snapshots),
    };
}

} // namespace

std::expected<IncrementalCompileResult, IncrementalCompileError>
MsvcIncrementalCompileCoordinator::run(const IncrementalCompileRequest& request) const {
    IncrementalCompileResult result;

    msvc::CompileExecutionRequest execution_request{
        .unit = request.unit,
        .options = request.options,
        .source_dependencies_file = request.source_dependencies_file,
        .working_directory = request.working_directory,
    };
    auto recipe = executor_.build_recipe(execution_request);
    if (!recipe) {
        return std::unexpected(IncrementalCompileError{
            .code = IncrementalCompileErrorCode::compile_failed,
            .message = "failed to construct MSVC compile recipe",
            .compile_error = recipe.error(),
        });
    }
    result.recipe = std::move(*recipe);

    // An upstream coordinator may already own the invalidation decision (for
    // example, a successfully rebuilt MQB-owned PCH). In that case old cache
    // state cannot make this unit reusable, so avoid probing it and reseal the
    // normal compile cache from the compiler result below.
    if (request.force_rebuild) {
        result.validation.reasons.push_back(BuildReason::explicit_rebuild);
    } else {
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
                auto dependency_snapshot = detail::snapshot_file_or_directory(dependency);
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

        const auto current_include_search_roots = msvc::include_search_roots(
            request.options,
            toolchain_.environment,
            request.working_directory);

        // Cache migration is owned by BuildSignature's compile domain. The v5
        // domain invalidates pre-closure entries exactly once, including recipes
        // with zero include roots and zero directory freshness evidence. Do not
        // infer cache generation from whether those evidence vectors are empty.

        // CompilerOptions already identify typed/native /I ordering, but vcvars
        // INCLUDE is ambient toolchain environment rather than argv. Compare the
        // exact ordered roots persisted by cache v4 so root replacement/removal is
        // freshness even when all previously resolved headers still exist.
        if (cached_entry
            && !same_ordered_paths(
                cached_entry->include_search_roots,
                current_include_search_roots)) {
            add_reason_once(result.validation.reasons, BuildReason::dependency_changed);
        }

        // The validator is authoritative for incremental freshness. Once it says
        // this compile is reusable there is no action for the generic planner to
        // derive, so return directly on the hot no-op path. Recipe construction
        // already happened above, so a warm hit remains fully inspectable without
        // launching the compiler.
        if (result.validation.reusable()) {
            return result;
        }
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

    auto executed = executor_.execute(*result.recipe);
    if (!executed) {
        return std::unexpected(IncrementalCompileError{
            .code = IncrementalCompileErrorCode::compile_failed,
            .message = "planned MSVC compile action failed",
            .compile_error = executed.error(),
        });
    }

    result.compiled = true;
    result.process = std::move(executed->process);

    seal_module_scan_evidence(
        executed->cache_entry,
        request,
        toolchain_,
        result.warnings);

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
