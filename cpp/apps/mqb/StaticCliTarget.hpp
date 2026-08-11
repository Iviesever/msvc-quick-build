#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/orchestration/MsvcIncrementalTargetCoordinator.hpp"
#include "mqb/process/Process.hpp"

namespace mqb::cli {

struct StaticCliTargetRequest {
    std::vector<orchestration::TargetSourceRequest> sources;
    TargetArtifacts target;
    CompilerOptions compiler_options;
    std::filesystem::path project_root;
    std::string target_name;
    std::size_t max_parallel_jobs{1};
    bool verbose{false};
};

[[nodiscard]] int run_static_target(
    StaticCliTargetRequest request,
    const msvc::MsvcToolchain& toolchain,
    process::ProcessRunner& runner);

} // namespace mqb::cli
