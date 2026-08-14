#include "Application.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "Cli.hpp"
#include "Diagnostics.hpp"
#include "Invocation.hpp"
#include "ModuleCliTarget.hpp"
#include "PerformanceTimings.hpp"
#include "ProjectSetup.hpp"
#include "StaticCliTarget.hpp"
#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/LinkOptions.hpp"
#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/discovery/SourceDiscovery.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcLinker.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalTargetCoordinator.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"
#include "mqb/process/Process.hpp"

namespace mqb::app {
namespace {

namespace fs = std::filesystem;

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

[[nodiscard]] bool inside_project(
    const fs::path& project_root,
    const fs::path& path) {
    return safe_relative(path.lexically_normal().lexically_relative(project_root.lexically_normal()));
}

[[nodiscard]] fs::path display_source(
    const fs::path& project_root,
    const fs::path& source) {
    const fs::path relative = source.lexically_relative(project_root);
    return safe_relative(relative) ? relative : source;
}

void add_portable_root_if_missing(
    std::vector<fs::path>& roots,
    const fs::path& candidate) {
    if (candidate.empty()) {
        return;
    }
    const fs::path normalized = candidate.lexically_normal();
    if (std::find(roots.begin(), roots.end(), normalized) == roots.end()) {
        roots.push_back(normalized);
    }
}

[[nodiscard]] std::string message_with_path(
    std::string message,
    const fs::path& path) {
    if (!path.empty()) {
        message += ": ";
        message += diagnostics::path_text(path);
    }
    return message;
}

} // namespace

int Application::run(const std::span<const std::string_view> arguments) {
    const auto application_started = performance::Clock::now();

    auto parsed = mqb::cli::parse_arguments(arguments);
    if (!parsed) {
        std::cerr << "error: " << parsed.error().message << "\n\n" << mqb::cli::usage();
        return 2;
    }
    auto options = std::move(*parsed);
    if (options.show_help) {
        std::cout << mqb::cli::usage();
        return 0;
    }

    performance::Session timing_session{options.timings, application_started};

    auto invocation = resolve_invocation(options);
    if (!invocation) {
        diagnostics::print_error(invocation.error());
        return 2;
    }

    auto project = prepare_project(options, invocation->directory);
    if (!project) {
        if (project.error().config_error) {
            diagnostics::print_config_error(*project.error().config_error);
        } else {
            diagnostics::print_error(project.error().message);
        }
        return 2;
    }

    auto& project_config = project->config;
    auto& effective = project->effective;
    const fs::path& project_root = project->project_root;
    const auto& requested_sources = invocation->requested_sources;

    std::vector<fs::path> sources = requested_sources;
    bool discovery_requires_module_pipeline = false;
    if (options.discover_sources
        && requested_sources.size() == 1
        && !mqb::cli::is_module_interface_source(requested_sources.front())) {
        const fs::path& entry = requested_sources.front();
        const bool project_scoped = inside_project(project_root, entry);
        const fs::path discovery_root = project_scoped
            ? project_root
            : entry.parent_path();
        mqb::discovery::Request discovery_request{
            .project_root = discovery_root,
            .entry = entry,
            .include_directories = options.include_directories,
        };
        if (project_scoped) {
            discovery_request.excluded_directories = effective.discovery_exclude_directories;
            discovery_request.extra_sources = effective.discovery_extra_sources;
            discovery_request.excluded_sources = effective.discovery_exclude_sources;
        }
        const auto discovery_started = performance::Clock::now();
        auto discovered = mqb::discovery::SourceDiscovery::discover(discovery_request);
        timing_session.add_discovery(performance::Clock::now() - discovery_started);
        if (!discovered) {
            diagnostics::print_error(message_with_path(
                "source discovery failed: " + discovered.error().message,
                discovered.error().path));
            return 2;
        }
        for (const auto& warning : discovered->warnings) {
            diagnostics::print_warning(message_with_path(
                "source discovery: " + warning.message,
                warning.path));
        }
        discovery_requires_module_pipeline = discovered->requires_module_pipeline;
        sources = std::move(discovered->sources);
        if (sources.size() > 1 || options.verbose) {
            std::cout << "[discover] " << sources.size() << " translation units";
            if (options.verbose) {
                std::cout << " from " << discovered->indexed_files << " indexed C/C++ files";
            }
            std::cout << '\n';
        }
    }
    options.build.sources = sources;

    const unsigned int hardware_threads = std::thread::hardware_concurrency();
    const std::size_t requested_compile_jobs = options.jobs.value_or(
        hardware_threads == 0
            ? std::size_t{1}
            : static_cast<std::size_t>(hardware_threads));
    const std::size_t compile_jobs = std::max<std::size_t>(
        1,
        std::min(requested_compile_jobs, sources.size()));

    auto layout = mqb::ProjectArtifactLayout::create(project_root);
    if (!layout) {
        diagnostics::print_error(layout.error().message);
        return 2;
    }

    std::vector<mqb::orchestration::TargetSourceRequest> target_sources;
    target_sources.reserve(sources.size());
    for (const auto& source : sources) {
        auto artifacts = layout->for_source(source);
        if (!artifacts) {
            diagnostics::print_error(
                artifacts.error().message + ": " + diagnostics::path_text(source));
            return 2;
        }
        target_sources.push_back(mqb::orchestration::TargetSourceRequest{
            .source = source,
            .artifacts = std::move(*artifacts),
        });
    }

    const std::string target_name = options.build.output_name.value_or(
        requested_sources.front().stem().string());
    auto target_artifacts = layout->for_target(target_name, options.build.target_kind);
    if (!target_artifacts) {
        diagnostics::print_error(target_artifacts.error().message);
        return 2;
    }

    add_portable_root_if_missing(options.portable_roots, project_root / "portable_msvc");
    for (const auto& source : sources) {
        add_portable_root_if_missing(options.portable_roots, source.parent_path() / "portable_msvc");
    }

    mqb::platform::windows::WindowsProcessRunner runner;
    mqb::msvc::MsvcToolchainLocator locator{runner};
    mqb::msvc::DiscoveryOptions toolchain_discovery;
    toolchain_discovery.target_architecture = options.build.architecture;
    toolchain_discovery.host_architecture = mqb::Architecture::x64;
    toolchain_discovery.preference = options.toolchain_preference;
    toolchain_discovery.portable_roots = options.portable_roots;

    auto toolchain = locator.discover(toolchain_discovery);
    if (!toolchain) {
        diagnostics::print_error(message_with_path(
            toolchain.error().message,
            toolchain.error().path));
        return 3;
    }

    mqb::CompilerOptions compiler_options;
    compiler_options.configuration = options.build.configuration;
    compiler_options.architecture = options.build.architecture;
    compiler_options.standard = options.build.standard;
    compiler_options.runtime_library = options.runtime_override;
    compiler_options.link_time_code_generation = effective.link_time_code_generation;
    compiler_options.defines = std::move(options.defines);
    compiler_options.include_directories = std::move(options.include_directories);
    compiler_options.additional_arguments = std::move(options.compiler_arguments);
    compiler_options.external_module_providers = options.external_module_providers;

    const bool has_external_module_providers = !compiler_options.external_module_providers.empty();
    const bool module_target = has_external_module_providers
        || discovery_requires_module_pipeline
        || std::any_of(
            target_sources.begin(),
            target_sources.end(),
            [](const mqb::orchestration::TargetSourceRequest& source) {
                return mqb::cli::is_module_interface_source(source.source);
            });

    if (options.build.target_kind == mqb::TargetKind::static_library) {
        if (module_target) {
            diagnostics::print_error(
                "static-library targets do not yet support the Modules/Header Unit pipeline");
            return 2;
        }
        if (project->subsystem_explicit
            || !options.library_directories.empty()
            || !options.libraries.empty()
            || !options.linker_arguments.empty()) {
            diagnostics::print_error(
                "static-library targets do not accept linker-only policy "
                "(subsystem, library paths/libraries, or linker args)");
            return 2;
        }
        return mqb::cli::run_static_target(
            mqb::cli::StaticCliTargetRequest{
                .sources = std::move(target_sources),
                .target = std::move(*target_artifacts),
                .compiler_options = std::move(compiler_options),
                .project_root = project_root,
                .target_name = target_name,
                .max_parallel_jobs = compile_jobs,
                .timings = &timing_session,
                .verbose = options.verbose,
            },
            *toolchain,
            runner);
    }

    mqb::LinkOptions link_options;
    link_options.configuration = options.build.configuration;
    link_options.architecture = options.build.architecture;
    link_options.target_kind = options.build.target_kind;
    link_options.subsystem = options.subsystem_override.value_or(mqb::LinkSubsystem::console);
    link_options.link_time_code_generation = effective.link_time_code_generation;
    link_options.library_directories = std::move(options.library_directories);
    link_options.libraries = std::move(options.libraries);
    link_options.additional_arguments = std::move(options.linker_arguments);

    if (module_target) {
        return mqb::cli::run_module_target(
            mqb::cli::ModuleCliTargetRequest{
                .sources = std::move(target_sources),
                .target = std::move(*target_artifacts),
                .compiler_options = std::move(compiler_options),
                .link_options = std::move(link_options),
                .project_root = project_root,
                .config_file = project_config
                    ? std::optional<fs::path>{project_config->file}
                    : std::nullopt,
                .target_name = target_name,
                .max_parallel_jobs = compile_jobs,
                .timings = &timing_session,
                .jobs_explicit = options.jobs.has_value(),
                .force_named_modules = discovery_requires_module_pipeline
                    || has_external_module_providers,
                .verbose = options.verbose,
                .run_after_build = options.build.run_after_build,
                .run_arguments = std::move(options.build.run_arguments),
            },
            *toolchain,
            runner);
    }

    if (options.verbose) {
        std::cout << "[target] " << target_name << "\n"
                  << "  project: " << diagnostics::path_text(project_root) << '\n';
        if (project_config) {
            std::cout << "  config:  " << diagnostics::path_text(project_config->file) << '\n';
        }
        std::cout << "  type:    " << mqb::to_string(options.build.target_kind) << '\n'
                  << "  ltcg:    " << (effective.link_time_code_generation ? "on" : "off") << '\n'
                  << "  jobs:    " << compile_jobs
                  << (options.jobs ? "" : " (auto)") << '\n'
                  << "  cl:      " << diagnostics::path_text(toolchain->identity.compiler) << '\n'
                  << "  link:    " << diagnostics::path_text(toolchain->linker) << '\n';
        for (const auto& source : target_sources) {
            std::cout << "  source:  " << diagnostics::path_text(display_source(project_root, source.source)) << '\n'
                      << "    obj:   " << diagnostics::path_text(source.artifacts.object) << '\n'
                      << "    deps:  " << diagnostics::path_text(source.artifacts.dependencies) << '\n'
                      << "    cache: " << diagnostics::path_text(source.artifacts.compile_cache) << '\n';
        }
        for (const auto& directory : link_options.library_directories) {
            std::cout << "  libpath: " << diagnostics::path_text(directory) << '\n';
        }
        for (const auto& library : link_options.libraries) {
            std::cout << "  lib:     " << library << '\n';
        }
        for (const auto& argument : compiler_options.additional_arguments) {
            std::cout << "  cl-arg:  " << argument << '\n';
        }
        for (const auto& argument : link_options.additional_arguments) {
            std::cout << "  link-arg:" << argument << '\n';
        }
        std::cout << "  output:  " << diagnostics::path_text(target_artifacts->executable) << '\n'
                  << "  cache:   " << diagnostics::path_text(target_artifacts->link_cache) << '\n';
    }

    mqb::msvc::MsvcCompileExecutor compile_executor{*toolchain, runner};
    mqb::orchestration::MsvcIncrementalCompileCoordinator compile_coordinator{
        *toolchain,
        compile_executor};
    mqb::msvc::MsvcLinker linker{*toolchain, runner};
    mqb::orchestration::MsvcIncrementalLinkCoordinator link_coordinator{*toolchain, linker};
    mqb::orchestration::MsvcIncrementalTargetCoordinator target_coordinator{
        compile_coordinator,
        link_coordinator};

    const mqb::orchestration::IncrementalTargetRequest request{
        .sources = std::move(target_sources),
        .target = std::move(*target_artifacts),
        .compiler_options = std::move(compiler_options),
        .link_options = std::move(link_options),
        .working_directory = project_root,
        .max_parallel_compiles = compile_jobs,
    };

    auto result = target_coordinator.run(request);
    if (!result) {
        diagnostics::print_target_failure(result.error());
        return result.error().code == mqb::orchestration::IncrementalTargetErrorCode::link_failed ? 5 : 4;
    }

    timing_session.add_target(result->timings);
    for (const auto& compile : result->compiles) {
        timing_session.record_compile(compile.result.compiled);
    }
    timing_session.record_link(result->link.linked);

    for (const auto& compile : result->compiles) {
        diagnostics::print_compile_warnings(compile.result);
        const fs::path label = display_source(project_root, compile.source);
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
        std::cout << "[link] " << diagnostics::path_text(request.target.executable.filename());
        diagnostics::print_reasons(result->link.validation.reasons);
        std::cout << '\n';
        if (result->link.process) {
            diagnostics::print_process_output(*result->link.process);
        }
    } else {
        std::cout << "[up-to-date] " << diagnostics::path_text(request.target.executable.filename()) << '\n';
    }

    std::cout << "output: " << diagnostics::path_text(request.target.executable) << '\n';

    if (!options.build.run_after_build) {
        return 0;
    }

    std::cout << "[run] " << diagnostics::path_text(request.target.executable.filename()) << '\n';
    mqb::process::ProcessSpec run_spec;
    run_spec.executable = request.target.executable;
    run_spec.arguments = options.build.run_arguments;
    run_spec.working_directory = project_root;
    run_spec.capture_stdout = true;
    run_spec.capture_stderr = true;

    auto run_result = runner.run(run_spec);
    if (!run_result) {
        diagnostics::print_error(
            "failed to run executable: " + run_result.error().message);
        return 6;
    }
    timing_session.record_run_startup(run_result->launch_duration);
    diagnostics::print_process_output(*run_result);
    return run_result->exit_code;
}

} // namespace mqb::app