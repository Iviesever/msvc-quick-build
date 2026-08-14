#include "StaticCliTarget.hpp"

#include <iostream>
#include <utility>

#include "Diagnostics.hpp"
#include "PerformanceTimings.hpp"
#include "mqb/core/BuildTypes.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcLibrarian.hpp"
#include "mqb/orchestration/MsvcIncrementalArchiveCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalStaticTargetCoordinator.hpp"

namespace mqb::cli {
namespace {

namespace diagnostics = mqb::app::diagnostics;

} // namespace

int run_static_target(
    StaticCliTargetRequest request,
    const msvc::MsvcToolchain& toolchain,
    process::ProcessRunner& runner) {
    if (request.verbose) {
        std::cout << "[target] " << request.target_name << '\n'
                  << "  project: " << diagnostics::path_text(request.project_root) << '\n'
                  << "  type:    static\n"
                  << "  cl:      " << diagnostics::path_text(toolchain.identity.compiler) << '\n'
                  << "  lib:     " << diagnostics::path_text(toolchain.librarian) << '\n'
                  << "  output:  " << diagnostics::path_text(request.target.executable) << '\n'
                  << "  cache:   " << diagnostics::path_text(request.target.link_cache) << '\n';
    }

    msvc::MsvcCompileExecutor compile_executor{toolchain, runner};
    orchestration::MsvcIncrementalCompileCoordinator compile_coordinator{
        toolchain, compile_executor};
    msvc::MsvcLibrarian librarian{toolchain, runner};
    orchestration::MsvcIncrementalArchiveCoordinator archive_coordinator{toolchain, librarian};
    orchestration::MsvcIncrementalStaticTargetCoordinator target_coordinator{
        compile_coordinator, archive_coordinator};

    auto result = target_coordinator.run(orchestration::IncrementalStaticTargetRequest{
        .sources = std::move(request.sources),
        .target = request.target,
        .compiler_options = std::move(request.compiler_options),
        .working_directory = request.project_root,
        .max_parallel_compiles = request.max_parallel_jobs,
    });
    if (!result) {
        diagnostics::print_static_target_failure(result.error());
        return result.error().code == orchestration::IncrementalStaticTargetErrorCode::archive_failed
            ? 5
            : 4;
    }

    if (request.timings) {
        request.timings->add_target(result->timings);
        for (const auto& compile : result->compiles) {
            request.timings->record_compile(compile.result.compiled);
        }
        request.timings->record_archive(result->archive.archived);
    }

    for (const auto& compile : result->compiles) {
        if (compile.result.compiled) {
            std::cout << "[compile] " << diagnostics::path_text(compile.source.filename());
            diagnostics::print_reasons(compile.result.validation.reasons);
            std::cout << '\n';
            if (compile.result.process) {
                diagnostics::print_process_output(*compile.result.process);
            }
        } else {
            std::cout << "[up-to-date] " << diagnostics::path_text(compile.source.filename())
                      << '\n';
        }
    }

    diagnostics::print_archive_warnings(result->archive);
    if (result->archive.archived) {
        std::cout << "[archive] " << diagnostics::path_text(request.target.executable.filename());
        diagnostics::print_reasons(result->archive.validation.reasons);
        std::cout << '\n';
        if (result->archive.process) {
            diagnostics::print_process_output(*result->archive.process);
        }
    } else {
        std::cout << "[up-to-date] " << diagnostics::path_text(request.target.executable.filename())
                  << '\n';
    }
    std::cout << "output: " << diagnostics::path_text(request.target.executable) << '\n';
    return 0;
}

} // namespace mqb::cli
