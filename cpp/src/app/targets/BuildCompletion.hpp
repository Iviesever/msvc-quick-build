#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mqb/core/CompilerOptions.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/process/Process.hpp"

namespace mqb::app {
namespace performance { class Session; }

// Application-private build/foreground boundary. Own every launch operand; no
// references to project, toolchain, or build-runner lifetimes cross this value.
// No request means successful build-only. Failure uses the original CLI code.
struct BuildCompletion {
    std::optional<process::ProcessSpec> foreground;
};
using BuildOutcome = std::expected<BuildCompletion, int>;

// Called only after the existing target has successfully built and reported.
// Capture the same runtime policy as before; toolchain environment is NOT
// blindly applied to the program (only the established ASAN PATH policy is).
[[nodiscard]] BuildCompletion complete_build(
    bool run_after_build,
    const std::filesystem::path& executable,
    std::vector<std::string> arguments,
    const std::filesystem::path& working_directory,
    const CompilerOptions& compiler_options,
    const msvc::MsvcToolchain& toolchain);

// Called by the foreground client AFTER build-side scope exit. Uses the supplied
// foreground runner; never inherits a build cancellation token/Job. This value
// is not a freshness snapshot, executable identity pin, or project write lease.
[[nodiscard]] int run_completed_build(
    const BuildOutcome& completion,
    process::ProcessRunner& foreground_runner,
    performance::Session* timings = nullptr);

} // namespace mqb::app
