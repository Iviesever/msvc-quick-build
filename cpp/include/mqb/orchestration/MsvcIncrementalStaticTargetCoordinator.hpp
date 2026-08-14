#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/orchestration/MsvcIncrementalArchiveCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalTargetCoordinator.hpp"
#include "mqb/orchestration/ParallelismPolicy.hpp"
#include "mqb/orchestration/TargetTimings.hpp"

namespace mqb::orchestration {

struct IncrementalStaticTargetRequest {
    std::vector<TargetSourceRequest> sources;
    std::vector<std::filesystem::path> additional_objects;
    TargetArtifacts target;
    CompilerOptions compiler_options;
    std::filesystem::path working_directory;
    ParallelismPolicy max_parallel_compiles{};
    bool force_downstream_rebuild{false};
};

enum class IncrementalStaticTargetErrorCode {
    no_sources,
    invalid_parallelism,
    duplicate_source,
    duplicate_object,
    duplicate_dependencies,
    duplicate_compile_cache,
    scheduling_failed,
    compile_failed,
    archive_failed,
};

struct IncrementalStaticTargetError {
    IncrementalStaticTargetErrorCode code{IncrementalStaticTargetErrorCode::no_sources};
    std::string message;
    std::filesystem::path source;
    std::optional<IncrementalCompileError> compile_error;
    std::optional<IncrementalArchiveError> archive_error;
};

struct IncrementalStaticTargetResult {
    std::vector<TargetCompileResult> compiles;
    IncrementalArchiveResult archive;
    TargetTimings timings;
    bool any_compiled{false};
};

class MsvcIncrementalStaticTargetCoordinator {
public:
    MsvcIncrementalStaticTargetCoordinator(
        MsvcIncrementalCompileCoordinator& compile_coordinator,
        MsvcIncrementalArchiveCoordinator& archive_coordinator)
        : compile_coordinator_(compile_coordinator), archive_coordinator_(archive_coordinator) {}

    [[nodiscard]] std::expected<IncrementalStaticTargetResult, IncrementalStaticTargetError>
    run(const IncrementalStaticTargetRequest& request) const;

private:
    MsvcIncrementalCompileCoordinator& compile_coordinator_;
    MsvcIncrementalArchiveCoordinator& archive_coordinator_;
};

} // namespace mqb::orchestration
