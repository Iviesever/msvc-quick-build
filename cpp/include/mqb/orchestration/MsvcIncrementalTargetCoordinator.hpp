#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/LinkOptions.hpp"
#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"
#include "mqb/orchestration/TargetTimings.hpp"

namespace mqb::orchestration {

struct TargetSourceRequest {
    std::filesystem::path source;
    SourceArtifacts artifacts;
};

struct IncrementalTargetRequest {
    std::vector<TargetSourceRequest> sources;
    TargetArtifacts target;
    CompilerOptions compiler_options;
    LinkOptions link_options;
    std::filesystem::path working_directory;
    std::size_t max_parallel_compiles{1};
};

struct TargetCompileResult {
    std::filesystem::path source;
    IncrementalCompileResult result;
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
};

struct IncrementalTargetError {
    IncrementalTargetErrorCode code{IncrementalTargetErrorCode::no_sources};
    std::string message;
    std::filesystem::path source;
    std::optional<IncrementalCompileError> compile_error;
    std::optional<IncrementalLinkError> link_error;
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

private:
    MsvcIncrementalCompileCoordinator& compile_coordinator_;
    MsvcIncrementalLinkCoordinator& link_coordinator_;
};

} // namespace mqb::orchestration
