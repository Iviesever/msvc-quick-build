#include <algorithm>
#include <expected>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "Cli.hpp"
#include "ModuleCliTarget.hpp"
#include "mqb/config/ProjectConfig.hpp"
#include "mqb/config/ProjectOptions.hpp"
#include "mqb/core/BuildTypes.hpp"
#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/LinkOptions.hpp"
#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/core/TranslationUnitClassifier.hpp"
#include "mqb/discovery/SourceDiscovery.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcLinker.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalTargetCoordinator.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"
#include "mqb/process/Process.hpp"

namespace {
namespace fs = std::filesystem;

[[nodiscard]] std::string path_text(const fs::path& path) {
    const auto bytes = path.generic_u8string();
    return std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

[[nodiscard]] std::expected<fs::path, std::string> absolute_path_from(
    const fs::path& base, const fs::path& path, const std::string_view description) {
    std::error_code error_code;
    const fs::path candidate = path.is_absolute() ? path : base / path;
    fs::path absolute = fs::absolute(candidate, error_code);
    if (error_code) return std::unexpected("failed to resolve " + std::string{description} + ": " + error_code.message());
    return absolute.lexically_normal();
}

[[nodiscard]] bool supported_source(const fs::path& source) { return mqb::is_translation_unit_path(source); }
[[nodiscard]] bool safe_relative(const fs::path& relative) {
    if (relative.empty() || relative.is_absolute() || relative == ".") return false;
    for (const auto& component : relative) if (component == "..") return false;
    return true;
}
[[nodiscard]] bool inside_project(const fs::path& project_root, const fs::path& path) {
    return safe_relative(path.lexically_normal().lexically_relative(project_root.lexically_normal()));
}
[[nodiscard]] fs::path display_source(const fs::path& project_root, const fs::path& source) {
    const fs::path relative = source.lexically_relative(project_root);
    return safe_relative(relative) ? relative : source;
}
void add_portable_root_if_missing(std::vector<fs::path>& roots, const fs::path& candidate) {
    if (candidate.empty()) return;
    const fs::path normalized = candidate.lexically_normal();
    if (std::find(roots.begin(), roots.end(), normalized) == roots.end()) roots.push_back(normalized);
}
void write_forwarded_text(std::ostream& stream, const std::string_view text) {
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char ch = text[index];
        if (ch == '\r' && index + 1 < text.size() && text[index + 1] == '\n') { stream.put('\n'); ++index; continue; }
        stream.put(ch);
    }
}
void print_process_output(const mqb::process::ProcessResult& process) {
    if (!process.stdout_text.empty()) {
        write_forwarded_text(std::cout, process.stdout_text);
        if (process.stdout_text.back() != '\n') std::cout << '\n';
    }
    if (!process.stderr_text.empty()) {
        write_forwarded_text(std::cerr, process.stderr_text);
        if (process.stderr_text.back() != '\n') std::cerr << '\n';
    }
}
void print_reasons(const std::vector<mqb::BuildReason>& reasons) {
    if (reasons.empty()) return;
    std::cout << " [";
    for (std::size_t index = 0; index < reasons.size(); ++index) {
        if (index != 0) std::cout << ", ";
        std::cout << mqb::to_string(reasons[index]);
    }
    std::cout << ']';
}
void print_config_error(const mqb::config::Error& error) {
    std::cerr << "error: project config: " << error.message;
    if (!error.path.empty()) {
        std::cerr << ": " << path_text(error.path);
        if (error.line != 0 && error.column != 0) std::cerr << ':' << error.line << ':' << error.column;
    }
    std::cerr << '\n';
}
void print_compile_failure(const mqb::orchestration::IncrementalCompileError& error) {
    std::cerr << "error: " << error.message << '\n';
    if (!error.compile_error) return;
    const auto& compile_error = *error.compile_error;
    std::cerr << "  " << compile_error.message << '\n';
    if (compile_error.compiler_error) {
        const auto& compiler_error = *compile_error.compiler_error;
        std::cerr << "  " << compiler_error.message << '\n';
        if (compiler_error.process_result) print_process_output(*compiler_error.process_result);
    }
    if (compile_error.dependency_error) std::cerr << "  " << compile_error.dependency_error->message << '\n';
}
void print_link_failure(const mqb::orchestration::IncrementalLinkError& error) {
    std::cerr << "error: " << error.message << '\n';
    if (error.library_resolution_error) {
        const auto& resolution = *error.library_resolution_error;
        std::cerr << "  " << resolution.message;
        if (!resolution.library.empty()) std::cerr << ": " << resolution.library;
        if (!resolution.path.empty()) std::cerr << " (" << path_text(resolution.path) << ')';
        std::cerr << '\n';
    }
    if (!error.linker_error) return;
    const auto& linker_error = *error.linker_error;
    std::cerr << "  " << linker_error.message << '\n';
    if (linker_error.process_result) print_process_output(*linker_error.process_result);
    if (linker_error.process_error) std::cerr << "  " << linker_error.process_error->message << '\n';
}
void print_target_failure(const mqb::orchestration::IncrementalTargetError& error) {
    std::cerr << "error: " << error.message;
    if (!error.source.empty()) std::cerr << ": " << path_text(error.source);
    std::cerr << '\n';
    if (error.compile_error) print_compile_failure(*error.compile_error);
    if (error.link_error) print_link_failure(*error.link_error);
}
void print_compile_warnings(const mqb::orchestration::IncrementalCompileResult& result) {
    for (const auto& warning : result.warnings) {
        std::cerr << "warning: " << warning.message;
        if (!warning.path.empty()) std::cerr << ": " << path_text(warning.path);
        std::cerr << '\n';
    }
}
void print_link_warnings(const mqb::orchestration::IncrementalLinkResult& result) {
    for (const auto& warning : result.warnings) {
        std::cerr << "warning: " << warning.message;
        if (!warning.path.empty()) std::cerr << ": " << path_text(warning.path);
        std::cerr << '\n';
    }
}
} // namespace

int main(const int argc, char* argv[]) {
    std::vector<std::string_view> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0u);
    for (int index = 1; index < argc; ++index) arguments.emplace_back(argv[index]);

    auto parsed = mqb::cli::parse_arguments(std::span<const std::string_view>{arguments});
    if (!parsed) { std::cerr << "error: " << parsed.error().message << "\n\n" << mqb::cli::usage(); return 2; }
    auto options = std::move(*parsed);
    if (options.show_help) { std::cout << mqb::cli::usage(); return 0; }

    std::error_code error_code;
    fs::path invocation_directory = fs::current_path(error_code);
    if (error_code) { std::cerr << "error: failed to resolve current working directory: " << error_code.message() << '\n'; return 2; }
    invocation_directory = invocation_directory.lexically_normal();

    std::vector<fs::path> requested_sources;
    requested_sources.reserve(options.build.sources.size());
    for (const auto& requested_source : options.build.sources) {
        auto source = absolute_path_from(invocation_directory, requested_source, "source file");
        if (!source) { std::cerr << "error: " << source.error() << '\n'; return 2; }
        if (!fs::is_regular_file(*source, error_code) || error_code) { std::cerr << "error: source file does not exist: " << path_text(*source) << '\n'; return 2; }
        if (!supported_source(*source)) { std::cerr << "error: only .c, .cpp, .cc, .cxx, .ixx, .cppm, and .mpp sources are supported: " << path_text(*source) << '\n'; return 2; }
        for (const auto& previous : requested_sources) {
            error_code.clear();
            if (fs::equivalent(previous, *source, error_code) && !error_code) { std::cerr << "error: source file was provided more than once: " << path_text(*source) << '\n'; return 2; }
        }
        requested_sources.push_back(std::move(*source));
    }

    for (auto& include_directory : options.include_directories) {
        auto resolved = absolute_path_from(invocation_directory, include_directory, "include directory");
        if (!resolved) { std::cerr << "error: " << resolved.error() << '\n'; return 2; }
        include_directory = std::move(*resolved);
    }
    for (auto& library_directory : options.library_directories) {
        auto resolved = absolute_path_from(invocation_directory, library_directory, "library directory");
        if (!resolved) { std::cerr << "error: " << resolved.error() << '\n'; return 2; }
        library_directory = std::move(*resolved);
    }
    for (auto& portable_root : options.portable_roots) {
        auto resolved = absolute_path_from(invocation_directory, portable_root, "portable toolchain root");
        if (!resolved) { std::cerr << "error: " << resolved.error() << '\n'; return 2; }
        portable_root = std::move(*resolved);
    }

    std::optional<mqb::config::ProjectConfig> project_config;
    auto located_config = mqb::config::ProjectConfigLoader::find_upwards(invocation_directory);
    if (!located_config) { print_config_error(located_config.error()); return 2; }
    if (located_config->has_value()) {
        auto loaded = mqb::config::ProjectConfigLoader::load(**located_config);
        if (!loaded) { print_config_error(loaded.error()); return 2; }
        project_config = std::move(*loaded);
    }
    const fs::path project_root = project_config ? project_config->project_root : invocation_directory;

    mqb::config::ProjectOverrides cli_overrides;
    cli_overrides.build.configuration = options.configuration_override;
    cli_overrides.build.architecture = options.architecture_override;
    cli_overrides.build.standard = options.standard_override;
    cli_overrides.build.output_name = options.build.output_name;
    cli_overrides.build.defines = options.defines;
    cli_overrides.build.include_directories = options.include_directories;
    cli_overrides.build.library_directories = options.library_directories;
    cli_overrides.build.libraries = options.libraries;
    cli_overrides.build.compiler_arguments = options.compiler_arguments;
    cli_overrides.build.linker_arguments = options.linker_arguments;
    cli_overrides.discovery.enabled = options.discovery_override;

    auto effective = mqb::config::resolve_project_options(project_config ? &*project_config : nullptr, cli_overrides);
    options.build.configuration = effective.configuration;
    options.build.architecture = effective.architecture;
    options.build.standard = effective.standard;
    options.build.output_name = effective.output_name;
    options.discover_sources = effective.discovery_enabled;
    options.defines = effective.defines;
    options.include_directories = effective.include_directories;
    options.library_directories = effective.library_directories;
    options.libraries = effective.libraries;
    options.compiler_arguments = effective.compiler_arguments;
    options.linker_arguments = effective.linker_arguments;

    std::vector<fs::path> sources = requested_sources;
    bool discovery_requires_module_pipeline = false;
    if (options.discover_sources && requested_sources.size() == 1
        && !mqb::cli::is_module_interface_source(requested_sources.front())) {
        const fs::path& entry = requested_sources.front();
        const bool project_scoped = inside_project(project_root, entry);
        const fs::path discovery_root = project_scoped ? project_root : entry.parent_path();
        mqb::discovery::Request discovery_request{.project_root = discovery_root, .entry = entry, .include_directories = options.include_directories};
        if (project_scoped) {
            discovery_request.excluded_directories = effective.discovery_exclude_directories;
            discovery_request.extra_sources = effective.discovery_extra_sources;
            discovery_request.excluded_sources = effective.discovery_exclude_sources;
        }
        auto discovered = mqb::discovery::SourceDiscovery::discover(discovery_request);
        if (!discovered) {
            std::cerr << "error: source discovery failed: " << discovered.error().message;
            if (!discovered.error().path.empty()) std::cerr << ": " << path_text(discovered.error().path);
            std::cerr << '\n'; return 2;
        }
        for (const auto& warning : discovered->warnings) {
            std::cerr << "warning: source discovery: " << warning.message;
            if (!warning.path.empty()) std::cerr << ": " << path_text(warning.path);
            std::cerr << '\n';
        }
        discovery_requires_module_pipeline = discovered->requires_module_pipeline;
        sources = std::move(discovered->sources);
        if (sources.size() > 1 || options.verbose) {
            std::cout << "[discover] " << sources.size() << " translation units";
            if (options.verbose) std::cout << " from " << discovered->indexed_files << " indexed C/C++ files";
            std::cout << '\n';
        }
    }
    options.build.sources = sources;

    const unsigned int hardware_threads = std::thread::hardware_concurrency();
    const std::size_t requested_compile_jobs = options.jobs.value_or(hardware_threads == 0 ? std::size_t{1} : static_cast<std::size_t>(hardware_threads));
    const std::size_t compile_jobs = std::max<std::size_t>(1, std::min(requested_compile_jobs, sources.size()));

    auto layout = mqb::ProjectArtifactLayout::create(project_root);
    if (!layout) { std::cerr << "error: " << layout.error().message << '\n'; return 2; }
    std::vector<mqb::orchestration::TargetSourceRequest> target_sources;
    target_sources.reserve(sources.size());
    for (const auto& source : sources) {
        auto artifacts = layout->for_source(source);
        if (!artifacts) { std::cerr << "error: " << artifacts.error().message << ": " << path_text(source) << '\n'; return 2; }
        target_sources.push_back(mqb::orchestration::TargetSourceRequest{.source = source, .artifacts = std::move(*artifacts)});
    }
    const std::string target_name = options.build.output_name.value_or(requested_sources.front().stem().string());
    auto target_artifacts = layout->for_target(target_name);
    if (!target_artifacts) { std::cerr << "error: " << target_artifacts.error().message << '\n'; return 2; }

    add_portable_root_if_missing(options.portable_roots, project_root / "portable_msvc");
    for (const auto& source : sources) add_portable_root_if_missing(options.portable_roots, source.parent_path() / "portable_msvc");

    mqb::platform::windows::WindowsProcessRunner runner;
    mqb::msvc::MsvcToolchainLocator locator{runner};
    mqb::msvc::DiscoveryOptions toolchain_discovery;
    toolchain_discovery.target_architecture = options.build.architecture;
    toolchain_discovery.host_architecture = mqb::Architecture::x64;
    toolchain_discovery.preference = options.toolchain_preference;
    toolchain_discovery.portable_roots = options.portable_roots;
    auto toolchain = locator.discover(toolchain_discovery);
    if (!toolchain) {
        std::cerr << "error: " << toolchain.error().message;
        if (!toolchain.error().path.empty()) std::cerr << ": " << path_text(toolchain.error().path);
        std::cerr << '\n'; return 3;
    }

    mqb::CompilerOptions compiler_options;
    compiler_options.configuration = options.build.configuration;
    compiler_options.architecture = options.build.architecture;
    compiler_options.standard = options.build.standard;
    compiler_options.runtime_library = options.runtime_override;
    compiler_options.defines = std::move(options.defines);
    compiler_options.include_directories = std::move(options.include_directories);
    compiler_options.additional_arguments = std::move(options.compiler_arguments);

    mqb::LinkOptions link_options;
    link_options.configuration = options.build.configuration;
    link_options.architecture = options.build.architecture;
    link_options.subsystem = options.subsystem_override.value_or(mqb::LinkSubsystem::console);
    link_options.library_directories = std::move(options.library_directories);
    link_options.libraries = std::move(options.libraries);
    link_options.additional_arguments = std::move(options.linker_arguments);

    const bool module_target = discovery_requires_module_pipeline || std::any_of(target_sources.begin(), target_sources.end(), [](const mqb::orchestration::TargetSourceRequest& source) { return mqb::cli::is_module_interface_source(source.source); });
    if (module_target) {
        return mqb::cli::run_module_target(
            mqb::cli::ModuleCliTargetRequest{
                .sources = std::move(target_sources), .target = std::move(*target_artifacts),
                .compiler_options = std::move(compiler_options), .link_options = std::move(link_options),
                .project_root = project_root,
                .config_file = project_config ? std::optional<fs::path>{project_config->file} : std::nullopt,
                .target_name = target_name, .max_parallel_jobs = compile_jobs,
                .jobs_explicit = options.jobs.has_value(), .force_named_modules = discovery_requires_module_pipeline,
                .verbose = options.verbose, .run_after_build = options.build.run_after_build,
                .run_arguments = std::move(options.build.run_arguments),
            }, *toolchain, runner);
    }

    if (options.verbose) {
        std::cout << "[target] " << target_name << "\n  project: " << path_text(project_root) << '\n';
        if (project_config) std::cout << "  config:  " << path_text(project_config->file) << '\n';
        std::cout << "  jobs:    " << compile_jobs << (options.jobs ? "" : " (auto)") << '\n'
                  << "  cl:      " << path_text(toolchain->identity.compiler) << '\n'
                  << "  link:    " << path_text(toolchain->linker) << '\n';
        for (const auto& source : target_sources) {
            std::cout << "  source:  " << path_text(display_source(project_root, source.source)) << '\n'
                      << "    obj:   " << path_text(source.artifacts.object) << '\n'
                      << "    deps:  " << path_text(source.artifacts.dependencies) << '\n'
                      << "    cache: " << path_text(source.artifacts.compile_cache) << '\n';
        }
        for (const auto& directory : link_options.library_directories) std::cout << "  libpath: " << path_text(directory) << '\n';
        for (const auto& library : link_options.libraries) std::cout << "  lib:     " << library << '\n';
        for (const auto& argument : compiler_options.additional_arguments) std::cout << "  cl-arg:  " << argument << '\n';
        for (const auto& argument : link_options.additional_arguments) std::cout << "  link-arg:" << argument << '\n';
        std::cout << "  exe:     " << path_text(target_artifacts->executable) << '\n'
                  << "  cache:   " << path_text(target_artifacts->link_cache) << '\n';
    }

    mqb::msvc::MsvcCompileExecutor compile_executor{*toolchain, runner};
    mqb::orchestration::MsvcIncrementalCompileCoordinator compile_coordinator{*toolchain, compile_executor};
    mqb::msvc::MsvcLinker linker{*toolchain, runner};
    mqb::orchestration::MsvcIncrementalLinkCoordinator link_coordinator{*toolchain, linker};
    mqb::orchestration::MsvcIncrementalTargetCoordinator target_coordinator{compile_coordinator, link_coordinator};
    const mqb::orchestration::IncrementalTargetRequest request{
        .sources = std::move(target_sources), .target = std::move(*target_artifacts),
        .compiler_options = std::move(compiler_options), .link_options = std::move(link_options),
        .working_directory = project_root, .max_parallel_compiles = compile_jobs,
    };
    auto result = target_coordinator.run(request);
    if (!result) { print_target_failure(result.error()); return result.error().code == mqb::orchestration::IncrementalTargetErrorCode::link_failed ? 5 : 4; }
    for (const auto& compile : result->compiles) {
        print_compile_warnings(compile.result);
        const fs::path label = display_source(project_root, compile.source);
        if (compile.result.compiled) {
            std::cout << "[compile] " << path_text(label); print_reasons(compile.result.validation.reasons); std::cout << '\n';
            if (compile.result.process) print_process_output(*compile.result.process);
        } else std::cout << "[up-to-date] " << path_text(label) << '\n';
    }
    print_link_warnings(result->link);
    if (result->link.linked) {
        std::cout << "[link] " << path_text(request.target.executable.filename()); print_reasons(result->link.validation.reasons); std::cout << '\n';
        if (result->link.process) print_process_output(*result->link.process);
    } else std::cout << "[up-to-date] " << path_text(request.target.executable.filename()) << '\n';
    std::cout << "executable: " << path_text(request.target.executable) << '\n';
    if (!options.build.run_after_build) return 0;
    std::cout << "[run] " << path_text(request.target.executable.filename()) << '\n';
    mqb::process::ProcessSpec run_spec;
    run_spec.executable = request.target.executable;
    run_spec.arguments = options.build.run_arguments;
    run_spec.working_directory = project_root;
    run_spec.capture_stdout = true;
    run_spec.capture_stderr = true;
    auto run_result = runner.run(run_spec);
    if (!run_result) { std::cerr << "error: failed to run executable: " << run_result.error().message << '\n'; return 6; }
    print_process_output(*run_result);
    return run_result->exit_code;
}