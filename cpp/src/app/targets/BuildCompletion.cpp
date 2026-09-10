#include "BuildCompletion.hpp"

#include <iostream>
#include <utility>

#include "Diagnostics.hpp"
#include "PerformanceTimings.hpp"
#include "mqb/msvc/MsvcAddressSanitizerPolicy.hpp"

namespace mqb::app {

BuildCompletion complete_build(
    const bool run_after_build,
    const std::filesystem::path& executable,
    std::vector<std::string> arguments,
    const std::filesystem::path& working_directory,
    const CompilerOptions& compiler_options,
    const msvc::MsvcToolchain& toolchain) {
    if (!run_after_build) return {};
    process::ProcessSpec spec;
    spec.executable = executable;
    spec.arguments = std::move(arguments);
    spec.working_directory = working_directory;
    spec.capture_stdout = true;
    spec.capture_stderr = true;
    // Inherit the foreground environment/stdin under the unchanged runner
    // contract. Only sanitizer-enabled builds add the existing runtime PATH.
    if (msvc::MsvcAddressSanitizerPolicy::compiler_enabled(
            compiler_options.additional_arguments)) {
        msvc::MsvcAddressSanitizerPolicy::apply_runtime_path(spec, toolchain);
    }
    return BuildCompletion{std::move(spec)};
}

int run_completed_build(
    const BuildOutcome& completion,
    process::ProcessRunner& foreground_runner,
    performance::Session* timings) {
    if (!completion) return completion.error(); // Already diagnosed by build.
    if (!completion->foreground) return 0;
    const auto& spec = *completion->foreground;
    if (spec.cancellation.stop_possible()) {
        diagnostics::print_error("foreground executable must not inherit build cancellation");
        return 6;
    }
    std::cout << "[run] " << diagnostics::path_text(spec.executable.filename()) << '\n';
    const auto result = foreground_runner.run(spec);
    if (!result) {
        diagnostics::print_error("failed to run executable: " + result.error().message);
        return 6;
    }
    if (timings) timings->record_run_startup(result->launch_duration);
    diagnostics::print_process_output(*result);
    return result->exit_code;
}

} // namespace mqb::app
