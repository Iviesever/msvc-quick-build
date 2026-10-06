#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mqb/core/BuildPlan.hpp"
#include "mqb/core/WriteInventory.hpp"
#include "mqb/core/BuildPlanner.hpp"
#include "mqb/core/CompileCache.hpp"
#include "mqb/core/CompileCacheFile.hpp"
#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/TranslationUnit.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/process/Process.hpp"

namespace mqb::orchestration {

namespace detail {
class TargetCompileWave;
}

struct IncrementalCompileRequest {
    TranslationUnit unit;
    CompilerOptions options;
    std::filesystem::path cache_file;
    std::filesystem::path source_dependencies_file;
    // Named-module sources carry the P1689 artifact that preceded this compile.
    // A successful compile can then seal scan-reuse evidence into its compile
    // cache. Ordinary and header-unit compile requests leave this empty.
    std::optional<std::filesystem::path> module_scan_output;
    std::optional<std::filesystem::path> working_directory;
    bool force_rebuild{false};
};

enum class IncrementalCompileWarningCode {
    cache_load_failed,
    cache_save_failed,
    file_snapshot_failed,
};

struct IncrementalCompileWarning {
    IncrementalCompileWarningCode code{IncrementalCompileWarningCode::file_snapshot_failed};
    std::filesystem::path path;
    std::string message;
};

enum class IncrementalCompileErrorCode {
    planning_failed,
    compile_failed,
    // Recorded calls only: refuse an inconsistent internal success instead of
    // fabricating a cache entry or repeating any filesystem/compiler operation.
    cache_evidence_unavailable,
};

struct IncrementalCompileError {
    IncrementalCompileErrorCode code{IncrementalCompileErrorCode::planning_failed};
    std::string message;
    std::optional<BuildPlannerError> planner_error;
    std::optional<msvc::CompileExecutorError> compile_error;
};

struct IncrementalCompileInspection {
    CompileCacheValidation validation;
    BuildPlan plan;
    std::vector<IncrementalCompileWarning> warnings;
};

struct IncrementalCompileResult : IncrementalCompileInspection {
    bool compiled{false};
    std::optional<process::ProcessResult> process;
};

// The state belongs to this invocation, not to the current contents of a path.
// Reuse does not save; save_failed still retains the entry offered to save().
enum class CompileCacheEvidenceState { reused, saved, save_failed };

struct CompileCacheEvidence {
    IncrementalCompileRequest request;
    // Coordinator context used for validation and scan sealing. An executor
    // supplied by the caller can have a different toolchain; cache_entry retains
    // that executor's actual entry without normalizing the discrepancy away.
    // Environment values remain in memory and must not be dumped as evidence.
    msvc::MsvcToolchain inspection_toolchain;
    CompileCacheEntry cache_entry;
    CompileCacheEvidenceState state{CompileCacheEvidenceState::reused};
    std::optional<CompileCacheFileError> save_error;

    static constexpr bool exact_cache_entry_captured = true;
    static constexpr bool producer_identity_verified = false;
    static constexpr bool current_content_verified = false;
    static constexpr bool complete_producer_inventory = false;
    static constexpr bool deletion_authorized = false;
};

struct RecordedIncrementalCompileResult {
    IncrementalCompileResult result;
    CompileCacheEvidence record;
};

class MsvcIncrementalCompileCoordinator {
public:
    MsvcIncrementalCompileCoordinator(
        const msvc::MsvcToolchain& toolchain,
        msvc::MsvcCompileExecutor& executor)
        : toolchain_(toolchain), executor_(executor) {}

    // Read cache/filesystem evidence and produce the exact incremental decision
    // without launching cl.exe or mutating cache/output state.
    [[nodiscard]] std::expected<IncrementalCompileInspection, IncrementalCompileError>
    inspect(const IncrementalCompileRequest& request) const;

    // Model every candidate output/cache before execution, without loading cache
    // or probing freshness. A returned error remains original typed recipe data;
    // the additive inventory also retains a gap. Never an execution ticket.
    [[nodiscard]] std::optional<msvc::CompileExecutorError>
    collect_known_writes(WriteInventory& out, const IncrementalCompileRequest& request) const;

    [[nodiscard]] std::expected<IncrementalCompileResult, IncrementalCompileError>
    run(const IncrementalCompileRequest& request) const;

    // Explicit opt-in. Move the final accepted/sealed cache value from this
    // invocation; never reload a path, rebuild a signature or compile twice.
    // Failure returns the original typed error, with no public success record.
    [[nodiscard]] std::expected<RecordedIncrementalCompileResult, IncrementalCompileError>
    run_recorded(const IncrementalCompileRequest& request) const;

private:
    friend class detail::TargetCompileWave;
    friend class MsvcIncrementalPchCoordinator;

    // Invocation-local handoff to PCH only, not a public execution ticket.
    // Capture the value accepted by this inspection without another cache read.
    [[nodiscard]] std::expected<IncrementalCompileInspection, IncrementalCompileError>
    inspect_for_pch_record(const IncrementalCompileRequest& request,
                          std::optional<CompileCacheEvidence>& accepted) const;
    // Private, invocation-owned wave seams. No public executable inspection or
    // reusable ticket. Hits take the entry from the first inspection; misses
    // execute that exact inspection once without a second cache load.
    [[nodiscard]] std::expected<IncrementalCompileInspection, IncrementalCompileError>
    inspect_for_target_record(const IncrementalCompileRequest& request,
                              std::optional<CompileCacheEvidence>& accepted,
                              std::optional<msvc::MsvcToolchain>& miss_context) const;
    [[nodiscard]] std::expected<IncrementalCompileResult, IncrementalCompileError>
    execute_inspected_for_target_record(const IncrementalCompileRequest& request,
                                       IncrementalCompileInspection inspection,
                                       msvc::MsvcToolchain inspection_context,
                                       std::optional<CompileCacheEvidence>& captured) const;
    struct CacheCapture;

    // Only run() and the invocation-owned target wave may consume a decision.
    // Public inspect() remains diagnostic data, not a reusable execution ticket.
    [[nodiscard]] std::expected<IncrementalCompileResult, IncrementalCompileError>
    execute_inspected(
        const IncrementalCompileRequest& request,
        IncrementalCompileInspection inspection) const;

    // Compile-time opt-in: the default instantiations contain no cache capture
    // or unconditional copies of request, environment, or cache entry vectors.
    template<bool Capture>
    [[nodiscard]] std::expected<IncrementalCompileInspection, IncrementalCompileError>
    inspect_impl(const IncrementalCompileRequest& request, CacheCapture* capture) const;

    template<bool Capture>
    [[nodiscard]] std::expected<IncrementalCompileResult, IncrementalCompileError>
    execute_inspected_impl(const IncrementalCompileRequest& request,
                           IncrementalCompileInspection inspection,
                           CacheCapture* capture) const;

    const msvc::MsvcToolchain& toolchain_;
    msvc::MsvcCompileExecutor& executor_;
};

} // namespace mqb::orchestration
