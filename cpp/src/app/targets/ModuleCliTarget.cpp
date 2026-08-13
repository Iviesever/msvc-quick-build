#include "ModuleCliTarget.hpp"

#include <filesystem>
#include <iostream>
#include <utility>
#include <vector>

#include "Diagnostics.hpp"
#include "mqb/core/BuildTypes.hpp"
#include "mqb/core/TranslationUnitClassifier.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcLinker.hpp"
#include "mqb/msvc/MsvcModuleDependencyScanner.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalTargetCoordinator.hpp"
#include "mqb/orchestration/MsvcModuleCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcModuleTargetCoordinator.hpp"
#include "mqb/orchestration/MsvcTargetRouter.hpp"

namespace mqb::cli {
namespace {

namespace fs = std::filesystem;
namespace diagnostics = mqb::app::diagnostics;

[[nodiscard]] bool safe_relative(const fs::path& relative) {
    if (relative.empty() || relative.is_absolute() || relative == ".") {
        return false;
    }
    for (const auto& component : relative) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

[[nodiscard]] fs::path display_source(const fs::path& root, const fs::path& source) {
    const fs::path relative = source.lexically_relative(root);
    return safe_relative(relative) ? relative : source;
}

} // namespace

bool is_module_interface_source(const fs::path& source) {
    const auto kind = classify_translation_unit_path(source);
    return kind && *kind == TranslationUnitKind::module_interface;
}

int run_module_target(
    ModuleCliTargetRequest request,
    const msvc::MsvcToolchain& toolchain,
    process::ProcessRunner& runner) {
    std::vector<orchestration::RoutedTargetSourceRequest> routed_sources;
    routed_sources.reserve(request.sources.size());
    for (auto& source : request.sources) {
        const auto kind = classify_translation_unit_path(source.source);
        if (!kind) {
            diagnostics::print_error(
                "unsupported translation unit: " + diagnostics::path_text(source.source));
            return 2;
        }
        routed_sources.push_back(orchestration::RoutedTargetSourceRequest{
            .source = source.source,
            .artifacts = std::move(source.artifacts),
            .kind = *kind,
        });
    }

    if (request.verbose) {
        std::cout << "[target] " << request.target_name << "\n"
                  << "  project: " << diagnostics::path_text(request.project_root) << '\n';
        if (request.config_file) {
            std::cout << "  config:  " << diagnostics::path_text(*request.config_file) << '\n';
        }
        std::cout << "  pipeline: named-modules\n"
                  << "  type:    " << to_string(request.link_options.target_kind) << '\n'
                  << "  jobs:    " << request.max_parallel_jobs
                  << (request.jobs_explicit ? "" : " (auto)") << '\n'
                  << "  cl:      " << diagnostics::path_text(toolchain.identity.compiler) << '\n'
                  << "  link:    " << diagnostics::path_text(toolchain.linker) << '\n';
        for (const auto& source : routed_sources) {
            std::cout << "  source:  "
                      << diagnostics::path_text(display_source(request.project_root, source.source))
                      << '\n'
                      << "    kind:  "
                      << (source.kind == TranslationUnitKind::module_interface
                              ? "module-interface"
                              : "source")
                      << '\n'
                      << "    obj:   " << diagnostics::path_text(source.artifacts.object) << '\n'
                      << "    deps:  " << diagnostics::path_text(source.artifacts.dependencies) << '\n'
                      << "    scan:  "
                      << diagnostics::path_text(source.artifacts.module_dependencies) << '\n';
            if (source.kind == TranslationUnitKind::module_interface) {
                std::cout << "    ifc:   "
                          << diagnostics::path_text(source.artifacts.module_interface) << '\n';
            }
            std::cout << "    cache: "
                      << diagnostics::path_text(source.artifacts.compile_cache) << '\n';
        }
        for (const auto& directory : request.link_options.library_directories) {
            std::cout << "  libpath: " << diagnostics::path_text(directory) << '\n';
        }
        for (const auto& library : request.link_options.libraries) {
            std::cout << "  lib:     " << library << '\n';
        }
        std::cout << "  output:  " << diagnostics::path_text(request.target.executable) << '\n'
                  << "  cache:   " << diagnostics::path_text(request.target.link_cache) << '\n';
    }

    msvc::MsvcCompileExecutor compile_executor{toolchain, runner};
    orchestration::MsvcIncrementalCompileCoordinator incremental_compile{toolchain, compile_executor};
    msvc::MsvcLinker linker{toolchain, runner};
    orchestration::MsvcIncrementalLinkCoordinator incremental_link{toolchain, linker};
    orchestration::MsvcIncrementalTargetCoordinator ordinary_target{
        incremental_compile, incremental_link};
    msvc::MsvcModuleDependencyScanner scanner{toolchain, runner};
    orchestration::MsvcModuleCompileCoordinator module_compile{incremental_compile};
    orchestration::MsvcModuleTargetCoordinator module_target{
        scanner, module_compile, incremental_link};
    orchestration::MsvcTargetRouter router{ordinary_target, module_target};

    auto artifact_layout = ProjectArtifactLayout::create(request.project_root);
    if (!artifact_layout) {
        diagnostics::print_error(artifact_layout.error().message);
        return 2;
    }

    orchestration::RoutedTargetRequest target_request{
        .sources = std::move(routed_sources),
        .target = request.target,
        .artifact_layout = std::move(*artifact_layout),
        .compiler_options = std::move(request.compiler_options),
        .link_options = std::move(request.link_options),
        .working_directory = request.project_root,
        .max_parallel_jobs = request.max_parallel_jobs,
        .force_named_modules = request.force_named_modules,
    };
    auto result = router.run(target_request);
    if (!result) {
        if (result.error().ordinary_error) {
            const auto& ordinary = *result.error().ordinary_error;
            diagnostics::print_target_failure(ordinary);
            return ordinary.code == orchestration::IncrementalTargetErrorCode::link_failed ? 5 : 4;
        }
        if (result.error().module_error) {
            const auto& module = *result.error().module_error;
            diagnostics::print_module_target_failure(module);
            return module.code == orchestration::IncrementalModuleTargetErrorCode::link_failed ? 5 : 4;
        }
        diagnostics::print_error(result.error().message);
        return 4;
    }

    for (const auto& compile : result->compiles) {
        diagnostics::print_compile_warnings(compile.result);
        const fs::path label = display_source(request.project_root, compile.source);
        if (compile.result.compiled) {
            std::cout << "[compile] " << diagnostics::path_text(label);
            diagnostics::print_reasons(compile.result.validation.reasons);
            std::cout << '\n';
            if (compile.result.process) {
                diagnostics::print_process_output(*compile.result.process);
            }
        } else {
            std::cout << "[up-to-date] " << diagnostics::path_text(label) << '\n';
        }
    }

    diagnostics::print_link_warnings(result->link);
    if (result->link.linked) {
        std::cout << "[link] " << diagnostics::path_text(target_request.target.executable.filename());
        diagnostics::print_reasons(result->link.validation.reasons);
        std::cout << '\n';
        if (result->link.process) {
            diagnostics::print_process_output(*result->link.process);
        }
    } else {
        std::cout << "[up-to-date] "
                  << diagnostics::path_text(target_request.target.executable.filename()) << '\n';
    }
    std::cout << "output: " << diagnostics::path_text(target_request.target.executable) << '\n';

    if (!request.run_after_build) {
        return 0;
    }
    std::cout << "[run] " << diagnostics::path_text(target_request.target.executable.filename())
              << '\n';
    process::ProcessSpec run_spec;
    run_spec.executable = target_request.target.executable;
    run_spec.arguments = std::move(request.run_arguments);
    run_spec.working_directory = request.project_root;
    run_spec.capture_stdout = true;
    run_spec.capture_stderr = true;
    auto run_result = runner.run(run_spec);
    if (!run_result) {
        diagnostics::print_error("failed to run executable: " + run_result.error().message);
        return 6;
    }
    diagnostics::print_process_output(*run_result);
    return run_result->exit_code;
}

} // namespace mqb::cli
