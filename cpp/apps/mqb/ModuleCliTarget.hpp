#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/LinkOptions.hpp"
#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/orchestration/MsvcIncrementalTargetCoordinator.hpp"
#include "mqb/process/Process.hpp"

namespace mqb::cli {

[[nodiscard]] bool is_module_interface_source(const std::filesystem::path& source);

struct ModuleCliTargetRequest {
    std::vector<orchestration::TargetSourceRequest> sources;
    TargetArtifacts target;
    CompilerOptions compiler_options;
    LinkOptions link_options;
    std::filesystem::path project_root;
    std::optional<std::filesystem::path> config_file;
    std::string target_name;
    std::size_t max_parallel_jobs{1};
    bool jobs_explicit{false};
    bool force_named_modules{false};
    bool verbose{false};
    bool run_after_build{false};
    std::vector<std::string> run_arguments;
};

[[nodiscard]] int run_module_target(
    ModuleCliTargetRequest request,
    const msvc::MsvcToolchain& toolchain,
    process::ProcessRunner& runner);

} // namespace mqb::cli
