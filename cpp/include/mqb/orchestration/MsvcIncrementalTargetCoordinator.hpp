#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <stop_token>
#include <optional>
#include <string>
#include <vector>

#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/LinkOptions.hpp"
#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/orchestration/BoundedWorkBatch.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"
#include "mqb/orchestration/ParallelismPolicy.hpp"
#include "mqb/orchestration/TargetTimings.hpp"

namespace mqb::orchestration {

struct TargetSourceRequest {
    std::filesystem::path source;
    SourceArtifacts artifacts;
};

struct IncrementalTargetRequest {
    std::vector<TargetSourceRequest> sources;
    // MQB-owned objects created outside the ordinary source scheduler (for
    // example the object paired with a first-class PCH creator). They enter the
    // final link signature/order but are never compiled as consumer sources.
    std::vector<std::filesystem::path> additional_objects;
    TargetArtifacts target;
    CompilerOptions compiler_options;
    LinkOptions link_options;
    std::filesystem::path working_directory;
    ParallelismPolicy max_parallel_compiles{};
    bool force_downstream_rebuild{false};
};

struct TargetCompileResult {
    std::filesystem::path source;
    IncrementalCompileResult result;
};

// Opt-in failure/cancellation evidence. Each false phase result refers to the
// exact source slot below; callback/setup exceptions remain in the typed batch.
// Execution slots map through execution_sources (inspection uses source order).
// A successful TU may save its own valid cache before a sibling stops the wave.
// This is not rollback, external-writer completion, or a write-lease certificate.
struct TargetCompileFailure {
    std::size_t source_index{};
};
using TargetCompilePhase = WorkBatchReport<std::expected<void, TargetCompileFailure>>;
struct TargetCompileWaveEvidence {
    std::vector<std::optional<std::expected<IncrementalCompileResult, IncrementalCompileError>>> attempts;
    std::optional<TargetCompilePhase> inspection;
    std::optional<TargetCompilePhase> execution;
    std::vector<std::size_t> execution_sources;
    std::exception_ptr setup_exception;
};
struct TargetAdmissionEvidence {
    // A second wave is the existing conservative freshness retry, not a retry
    // after a compile failure. Keep both passes when the second is interrupted.
    std::vector<TargetCompileWaveEvidence> waves;
};

enum class IncrementalTargetErrorCode {
    no_sources,
    invalid_parallelism,
    duplicate_source,
    duplicate_object,
    duplicate_dependencies,
    duplicate_compile_cache,
    scheduling_failed,
    compile_failed,
    link_failed,
    cancelled,
};

struct IncrementalTargetError {
    IncrementalTargetErrorCode code{IncrementalTargetErrorCode::no_sources};
    std::string message;
    std::filesystem::path source;
    std::optional<IncrementalCompileError> compile_error;
    std::optional<IncrementalLinkError> link_error;
    std::shared_ptr<const TargetAdmissionEvidence> admission;
};

struct IncrementalTargetResult {
    std::vector<TargetCompileResult> compiles;
    IncrementalLinkResult link;
    TargetTimings timings;
    bool any_compiled{false};
};

class MsvcIncrementalTargetCoordinator {
public:
    MsvcIncrementalTargetCoordinator(
        MsvcIncrementalCompileCoordinator& compile_coordinator,
        MsvcIncrementalLinkCoordinator& link_coordinator)
        : compile_coordinator_(compile_coordinator),
          link_coordinator_(link_coordinator) {}

    [[nodiscard]] std::expected<IncrementalTargetResult, IncrementalTargetError>
    run(const IncrementalTargetRequest& request) const;

    // Validation still precedes cancellation. Reuses the normal inspect/miss
    // execution and post-execution freshness barrier. A non-stoppable token
    // delegates run(). Failure wins over concurrent stop, with original records.
    // After a final stop check the terminal link stage is admitted as a whole;
    // later stop does not terminate that link or roll back its valid cache save.
    // No CLI caller opts in and no token is passed to ProcessSpec.
    [[nodiscard]] std::expected<IncrementalTargetResult, IncrementalTargetError>
    run_with_compile_admission_stop(
        const IncrementalTargetRequest& request, std::stop_token admission_stop) const;

private:
    template<bool WithAdmissionStop>
    [[nodiscard]] std::expected<IncrementalTargetResult, IncrementalTargetError>
    run_impl(const IncrementalTargetRequest& request, std::stop_token admission_stop) const;

    MsvcIncrementalCompileCoordinator& compile_coordinator_;
    MsvcIncrementalLinkCoordinator& link_coordinator_;
};

} // namespace mqb::orchestration
