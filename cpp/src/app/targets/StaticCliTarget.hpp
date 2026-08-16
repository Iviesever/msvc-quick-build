#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/orchestration/MsvcIncrementalTargetCoordinator.hpp"
#include "mqb/orchestration/ParallelismPolicy.hpp"
#include "mqb/process/Process.hpp"

namespace mqb::app::performance {
class Session;
}

namespace mqb::cli {

struct StaticCliTargetRequest {
    std::vector<orchestration::TargetSourceRequest> sources;
    std::vector<std::filesystem::path> additional_objects;
    TargetArtifacts target;
    CompilerOptions compiler_options;
    std::vector<std::string> librarian_arguments;
    std::filesystem::path project_root;
    std::string target_name;
    orchestration::ParallelismPolicy max_parallel_jobs{};
    app::performance::Session* timings{};
    bool force_downstream_rebuild{false};
    bool verbose{false};
};

[[nodiscard]] int run_static_target(
    StaticCliTargetRequest request,
    const msvc::MsvcToolchain& toolchain,
    process::ProcessRunner& runner);

} // namespace mqb::cli
