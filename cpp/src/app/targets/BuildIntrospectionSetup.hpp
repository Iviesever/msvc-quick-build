#pragma once

#include <expected>
#include <optional>
#include <string>
#include <vector>

#include "Cli.hpp"
#include "Invocation.hpp"
#include "ProjectSetup.hpp"
#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/orchestration/MsvcIncrementalTargetCoordinator.hpp"
#include "mqb/process/Process.hpp"

namespace mqb::app {

struct BuildIntrospectionPolicy {
    bool persistent_discovery_cache{true};
    bool persistent_toolchain_cache{true};
};

struct BuildIntrospectionError {
    int exit_code{2};
    std::string message;
    std::optional<mqb::config::Error> config_error;
};

struct BuildIntrospectionContext {
    mqb::cli::Options options;
    Invocation invocation;
    ProjectSetup project;
    mqb::ProjectArtifactLayout layout;
    std::vector<mqb::orchestration::TargetSourceRequest> target_sources;
    std::string target_name;
    mqb::TargetArtifacts target_artifacts;
    mqb::msvc::MsvcToolchain toolchain;
    mqb::CompilerOptions compiler_options;
    std::optional<mqb::PrecompiledHeaderArtifacts> pch_artifacts;
    bool module_target{false};

    [[nodiscard]] mqb::CompilerOptions consumer_compiler_options() const;
};

[[nodiscard]] std::expected<BuildIntrospectionContext, BuildIntrospectionError>
prepare_build_introspection(
    mqb::cli::Options options,
    mqb::process::ProcessRunner& runner,
    BuildIntrospectionPolicy policy = {});

} // namespace mqb::app
