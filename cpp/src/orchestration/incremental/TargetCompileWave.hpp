#pragma once

#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "mqb/core/Artifact.hpp"
#include "mqb/core/TranslationUnit.hpp"
#include "mqb/orchestration/BoundedWorkScheduler.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalTargetCoordinator.hpp"

#include "IncrementalFileSnapshot.hpp"

namespace mqb::orchestration::detail {

using TargetCompileAttempt =
    std::expected<IncrementalCompileResult, IncrementalCompileError>;

struct TargetCompileWaveSummary {
    // Absent for the historical small-target/forced-rebuild path. In that case
    // execution describes the original combined inspect-and-run callbacks.
    std::optional<BoundedWorkSummary> inspection;
    BoundedWorkSummary execution;
};

// Ordinary executable/DLL and static targets share this invocation-only owner.
// Pending requests and decisions never escape the wave, and only misses retain
// a request after inspection. The caller owns the final shared-evidence barrier
// and conservative whole-target retry, including mutations during execution.
class TargetCompileWave {
public:
    template <typename TargetRequest>
    [[nodiscard]] static std::expected<TargetCompileWaveSummary, BoundedWorkError>
    run(
        const TargetRequest& request,
        MsvcIncrementalCompileCoordinator& coordinator,
        const bool force_rebuild,
        FilesystemEvidenceTable* evidence_table,
        std::vector<std::optional<TargetCompileAttempt>>& attempts) {
        attempts.clear();
        attempts.resize(request.sources.size());

        if (evidence_table == nullptr || force_rebuild) {
            const auto direct = [&] {
                return BoundedWorkScheduler::run(
                    request.sources.size(), request.max_parallel_compiles,
                    [&](const std::size_t index) {
                        auto compile_request = make_request(
                            request.sources[index], request.compiler_options,
                            force_rebuild);
                        attempts[index].emplace(coordinator.run(compile_request));
                        return attempts[index]->has_value();
                    });
            };
            // Normal small targets make no TLS writes. Explicitly suspend an
            // inherited table for nested calls or a conservative retry.
            const auto scheduled = [&] {
                if (active_filesystem_evidence_table != nullptr) {
                    ScopedFilesystemEvidenceActivation suspended{nullptr};
                    return direct();
                }
                return direct();
            }();
            if (!scheduled) return std::unexpected(scheduled.error());
            return TargetCompileWaveSummary{
                .inspection = std::nullopt,
                .execution = *scheduled,
            };
        }

        // One pointer slot per TU; full request/plan storage is allocated only
        // for misses. Distinct callbacks own distinct slots, not a shared queue.
        std::vector<std::unique_ptr<PendingCompile>> pending(request.sources.size());
        const auto inspect_one = [&](const std::size_t index) {
            auto compile_request = make_request(
                request.sources[index], request.compiler_options, false);
            auto inspected = coordinator.inspect(compile_request);
            if (!inspected) {
                attempts[index].emplace(std::unexpected(std::move(inspected.error())));
                return false;
            }
            if (inspected->plan.actions.empty()) {
                IncrementalCompileResult hit;
                static_cast<IncrementalCompileInspection&>(hit) = std::move(*inspected);
                attempts[index].emplace(std::move(hit));
            } else {
                pending[index] = std::make_unique<PendingCompile>(PendingCompile{
                    .request = std::move(compile_request),
                    .inspection = std::move(*inspected),
                });
            }
            return true;
        };

        // Retain the proven parallel inspection policy. Zero execution workers
        // on an all-hit target does not mean zero inspection threads.
        const auto inspected = [&] {
            if (request.max_parallel_compiles == 1) {
                ScopedFilesystemEvidenceActivation active{evidence_table};
                return BoundedWorkScheduler::run(
                    request.sources.size(), request.max_parallel_compiles, inspect_one);
            }
            return BoundedWorkScheduler::run(
                request.sources.size(), request.max_parallel_compiles,
                [&](const std::size_t index) {
                    ScopedFilesystemEvidenceActivation active{evidence_table};
                    return inspect_one(index);
                });
        }();
        if (!inspected) return std::unexpected(inspected.error());
        TargetCompileWaveSummary summary{.inspection = *inspected};
        if (inspected->stop_requested) {
            // Target error selection remains in source order. No execution may
            // start after an inspection failure, even for already-planned misses.
            return summary;
        }

        std::vector<std::size_t> misses;
        for (std::size_t index = 0; index < pending.size(); ++index) {
            if (pending[index]) misses.push_back(index);
        }
        if (misses.empty()) return summary;

        const auto execute_one = [&](const std::size_t miss_index) {
            const std::size_t source_index = misses[miss_index];
            auto work = std::move(pending[source_index]);
            attempts[source_index].emplace(coordinator.execute_inspected(
                work->request, std::move(work->inspection)));
            return attempts[source_index]->has_value();
        };
        const auto executed = [&] {
            // Execution and cache sealing must not consume inspection snapshots.
            if (request.max_parallel_compiles == 1 || misses.size() == 1) {
                ScopedFilesystemEvidenceActivation suspended{nullptr};
                return BoundedWorkScheduler::run(
                    misses.size(), request.max_parallel_compiles, execute_one);
            }
            return BoundedWorkScheduler::run(
                misses.size(), request.max_parallel_compiles,
                [&](const std::size_t index) {
                    ScopedFilesystemEvidenceActivation suspended{nullptr};
                    return execute_one(index);
                });
        }();
        if (!executed) return std::unexpected(executed.error());
        summary.execution = *executed;
        return summary;
    }

private:
    struct PendingCompile {
        IncrementalCompileRequest request;
        IncrementalCompileInspection inspection;
    };

    [[nodiscard]] static IncrementalCompileRequest make_request(
        const TargetSourceRequest& source,
        const CompilerOptions& options,
        const bool force_rebuild) {
        TranslationUnit unit;
        unit.source = source.source;
        unit.kind = TranslationUnitKind::source;
        unit.outputs.push_back(Artifact{
            .path = source.artifacts.object,
            .kind = ArtifactKind::object,
        });
        return IncrementalCompileRequest{
            .unit = std::move(unit),
            .options = options,
            .cache_file = source.artifacts.compile_cache,
            .source_dependencies_file = source.artifacts.dependencies,
            .working_directory = source.source.parent_path(),
            .force_rebuild = force_rebuild,
        };
    }
};

} // namespace mqb::orchestration::detail
