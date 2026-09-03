#include "PlanCommand.hpp"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "Cli.hpp"
#include "Diagnostics.hpp"
#include "Invocation.hpp"
#include "ProjectSetup.hpp"
#include "mqb/core/Artifact.hpp"
#include "mqb/core/BuildTypes.hpp"
#include "mqb/core/LinkOptions.hpp"
#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/core/TranslationUnitClassifier.hpp"
#include "mqb/discovery/SourceDiscovery.hpp"
#include "mqb/msvc/MsvcAddressSanitizerPolicy.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcFuzzerPolicy.hpp"
#include "mqb/msvc/MsvcLinker.hpp"
#include "mqb/msvc/MsvcOpenMpPolicy.hpp"
#include "mqb/msvc/MsvcParameterCapabilities.hpp"
#include "mqb/msvc/MsvcParameterEngine.hpp"
#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalPchCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalTargetCoordinator.hpp"
#include "mqb/platform/windows/PathIdentity.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"
#include "mqb/process/Process.hpp"

namespace mqb::app {
namespace {

namespace fs = std::filesystem;

enum class PlanFormat { text, json };

struct ParsedPlanOptions {
    mqb::cli::Options build;
    PlanFormat format{PlanFormat::text};
};

struct PlanPhase {
    std::string kind;
    std::string label;
    fs::path output;
    bool build{false};
    std::vector<mqb::BuildReason> reasons;
    std::optional<mqb::process::ProcessSpec> process;
};

[[nodiscard]] std::string path_to_utf8(const fs::path& path) {
    const auto bytes = path.lexically_normal().generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()};
}

[[nodiscard]] bool inside_project(
    const fs::path& project_root,
    const fs::path& path) {
    return mqb::platform::windows::path_identity_contains(project_root, path);
}

void add_portable_root_if_missing(
    std::vector<fs::path>& roots,
    const fs::path& candidate) {
    if (candidate.empty()) return;
    const fs::path normalized = candidate.lexically_normal();
    const std::string key = mqb::platform::windows::path_identity_key(normalized);
    if (std::none_of(
            roots.begin(), roots.end(),
            [&](const fs::path& root) {
                return mqb::platform::windows::path_identity_key(root) == key;
            })) {
        roots.push_back(normalized);
    }
}

[[nodiscard]] bool is_module_interface_source(const fs::path& path) {
    const auto kind = mqb::classify_translation_unit_path(path);
    return kind && *kind == mqb::TranslationUnitKind::module_interface;
}

[[nodiscard]] bool validate_capabilities(
    const mqb::msvc::ParameterTool tool,
    const std::vector<std::string>& arguments,
    const std::string_view version) {
    for (const auto& argument : arguments) {
        const auto capability = mqb::msvc::MsvcParameterCapabilities::inspect(
            tool, argument, version);
        if (capability.lifecycle == mqb::msvc::ParameterLifecycle::active) continue;
        std::string message = "MSVC ";
        message += mqb::msvc::to_string(tool);
        message += " option '" + argument + "' is ";
        message += mqb::msvc::to_string(capability.lifecycle);
        message += " for toolset ";
        message += version;
        if (!capability.guidance.empty()) {
            message += ": ";
            message += capability.guidance;
        }
        if (capability.lifecycle == mqb::msvc::ParameterLifecycle::deprecated) {
            diagnostics::print_warning(message);
            continue;
        }
        diagnostics::print_error(message);
        return false;
    }
    return true;
}

[[nodiscard]] std::expected<ParsedPlanOptions, std::string>
parse_plan_arguments(const std::span<const std::string_view> arguments) {
    std::vector<std::string_view> forwarded;
    forwarded.reserve(arguments.size() + 1u);
    forwarded.emplace_back("build");
    PlanFormat format = PlanFormat::text;
    bool native_tail = false;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];
        if (!native_tail && (argument == "/link" || argument == "-link"
                || argument == "/lib" || argument == "/LIB")) {
            native_tail = true;
            forwarded.push_back(argument);
            continue;
        }
        if (!native_tail && argument == "--format") {
            if (index + 1 >= arguments.size()) {
                return std::unexpected("mqb plan --format requires text or json");
            }
            const auto value = arguments[++index];
            if (value == "text") format = PlanFormat::text;
            else if (value == "json") format = PlanFormat::json;
            else return std::unexpected("mqb plan --format must be text or json");
            continue;
        }
        if (!native_tail && argument.starts_with("--format=")) {
            const auto value = argument.substr(std::string_view{"--format="}.size());
            if (value == "text") format = PlanFormat::text;
            else if (value == "json") format = PlanFormat::json;
            else return std::unexpected("mqb plan --format must be text or json");
            continue;
        }
        forwarded.push_back(argument);
    }
    auto parsed = mqb::cli::parse_arguments(forwarded);
    if (!parsed) return std::unexpected(parsed.error().message);
    return ParsedPlanOptions{.build = std::move(*parsed), .format = format};
}

[[nodiscard]] std::string json_escape(const std::string_view value) {
    std::ostringstream escaped;
    escaped << std::hex << std::uppercase;
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': escaped << "\\\""; break;
        case '\\': escaped << "\\\\"; break;
        case '\b': escaped << "\\b"; break;
        case '\f': escaped << "\\f"; break;
        case '\n': escaped << "\\n"; break;
        case '\r': escaped << "\\r"; break;
        case '\t': escaped << "\\t"; break;
        default:
            if (ch < 0x20u) {
                escaped << "\\u00" << std::setw(2) << std::setfill('0')
                        << static_cast<unsigned int>(ch);
            } else {
                escaped << static_cast<char>(ch);
            }
        }
    }
    return escaped.str();
}

void render_text(
    const fs::path& project_root,
    const std::string_view target_name,
    const std::vector<PlanPhase>& phases) {
    std::cout << "plan: " << target_name << '\n'
              << "project: " << diagnostics::path_text(project_root) << '\n';
    for (const auto& phase : phases) {
        std::cout << '[' << (phase.build ? "build" : "up-to-date") << "] "
                  << phase.kind << ' ' << phase.label;
        if (!phase.reasons.empty()) {
            std::cout << " (";
            for (std::size_t index = 0; index < phase.reasons.size(); ++index) {
                if (index != 0) std::cout << ", ";
                std::cout << mqb::to_string(phase.reasons[index]);
            }
            std::cout << ')';
        }
        std::cout << '\n';
        if (!phase.output.empty()) {
            std::cout << "  output: " << diagnostics::path_text(phase.output) << '\n';
        }
        if (phase.process) {
            std::cout << "  exec: " << diagnostics::path_text(phase.process->executable);
            for (const auto& argument : phase.process->arguments) {
                std::cout << ' ' << argument;
            }
            std::cout << '\n';
        }
    }
}

void render_json(
    const fs::path& project_root,
    const std::string_view target_name,
    const std::vector<PlanPhase>& phases) {
    std::cout << "{\n"
              << "  \"project\": \"" << json_escape(path_to_utf8(project_root)) << "\",\n"
              << "  \"target\": \"" << json_escape(target_name) << "\",\n"
              << "  \"phases\": [\n";
    for (std::size_t index = 0; index < phases.size(); ++index) {
        const auto& phase = phases[index];
        std::cout << "    {\n"
                  << "      \"kind\": \"" << json_escape(phase.kind) << "\",\n"
                  << "      \"label\": \"" << json_escape(phase.label) << "\",\n"
                  << "      \"status\": \"" << (phase.build ? "build" : "up-to-date") << "\",\n"
                  << "      \"output\": \"" << json_escape(path_to_utf8(phase.output)) << "\",\n"
                  << "      \"reasons\": [";
        for (std::size_t reason_index = 0; reason_index < phase.reasons.size(); ++reason_index) {
            if (reason_index != 0) std::cout << ", ";
            std::cout << '"' << json_escape(mqb::to_string(phase.reasons[reason_index])) << '"';
        }
        std::cout << ']';
        if (phase.process) {
            std::cout << ",\n      \"process\": {\n"
                      << "        \"executable\": \""
                      << json_escape(path_to_utf8(phase.process->executable)) << "\",\n"
                      << "        \"arguments\": [";
            for (std::size_t arg_index = 0;
                 arg_index < phase.process->arguments.size(); ++arg_index) {
                if (arg_index != 0) std::cout << ", ";
                std::cout << '"' << json_escape(phase.process->arguments[arg_index]) << '"';
            }
            std::cout << "]\n      }\n";
        } else {
            std::cout << '\n';
        }
        std::cout << "    }" << (index + 1 == phases.size() ? "\n" : ",\n");
    }
    std::cout << "  ]\n}\n";
}

[[nodiscard]] mqb::orchestration::IncrementalCompileRequest compile_request_for(
    const mqb::orchestration::TargetSourceRequest& source,
    const mqb::CompilerOptions& options,
    const fs::path& project_root,
    const bool force_rebuild) {
    mqb::TranslationUnit unit;
    unit.source = source.source;
    unit.kind = mqb::TranslationUnitKind::source;
    unit.outputs = {mqb::Artifact{source.artifacts.object, mqb::ArtifactKind::object}};
    return mqb::orchestration::IncrementalCompileRequest{
        .unit = std::move(unit),
        .options = options,
        .cache_file = source.artifacts.compile_cache,
        .source_dependencies_file = source.artifacts.dependencies,
        .working_directory = project_root,
        .force_rebuild = force_rebuild,
    };
}

} // namespace

int run_plan_command(const std::span<const std::string_view> arguments) {
    auto parsed = parse_plan_arguments(arguments);
    if (!parsed) {
        diagnostics::print_error(parsed.error());
        return 2;
    }
    auto options = std::move(parsed->build);
    if (options.show_help) {
        std::cout << plan_usage();
        return 0;
    }

    auto invocation = resolve_invocation(options);
    if (!invocation) {
        diagnostics::print_error(invocation.error());
        return 2;
    }
    auto project = prepare_project(options, invocation->directory);
    if (!project) {
        if (project.error().config_error) diagnostics::print_config_error(*project.error().config_error);
        else diagnostics::print_error(project.error().message);
        return 2;
    }

    auto& project_config = project->config;
    auto& effective = project->effective;
    const fs::path& project_root = project->project_root;
    if (invocation->requested_sources.empty()) {
        auto entry = resolve_default_entry(
            project_root,
            project_config ? project_config->build.entry : std::nullopt);
        if (!entry) {
            diagnostics::print_error(entry.error());
            return 2;
        }
        invocation->requested_sources.push_back(std::move(*entry));
    }

    const auto& requested_sources = invocation->requested_sources;
    std::vector<fs::path> sources = requested_sources;
    bool discovery_requires_module_pipeline = false;
    if (options.discover_sources && requested_sources.size() == 1
        && !is_module_interface_source(requested_sources.front())) {
        auto forced_includes = mqb::msvc::MsvcParameterEngine::forced_includes(
            options.compiler_arguments);
        if (!forced_includes) {
            diagnostics::print_error(forced_includes.error().message);
            return 2;
        }
        if (effective.precompiled_header) {
            forced_includes->push_back(effective.precompiled_header->lexically_normal());
        }
        const fs::path& entry = requested_sources.front();
        const bool project_scoped = inside_project(project_root, entry);
        mqb::discovery::Request discovery_request{
            .project_root = project_scoped ? project_root : entry.parent_path(),
            .entry = entry,
            .include_directories = options.discovery_include_directories,
            .forced_includes = std::move(*forced_includes),
        };
        if (project_scoped) {
            discovery_request.excluded_directories = effective.discovery_exclude_directories;
            discovery_request.extra_sources = effective.discovery_extra_sources;
            discovery_request.excluded_sources = effective.discovery_exclude_sources;
        }
        auto discovered = mqb::discovery::SourceDiscovery::discover(discovery_request);
        if (!discovered) {
            diagnostics::print_error("source discovery failed: " + discovered.error().message);
            return 2;
        }
        discovery_requires_module_pipeline = discovered->requires_module_pipeline;
        sources = std::move(discovered->sources);
    }

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
            diagnostics::print_error(artifacts.error().message);
            return 2;
        }
        target_sources.push_back({.source = source, .artifacts = std::move(*artifacts)});
    }

    const bool module_target = !options.external_module_providers.empty()
        || discovery_requires_module_pipeline
        || std::any_of(target_sources.begin(), target_sources.end(),
            [](const auto& source) { return is_module_interface_source(source.source); });
    if (module_target) {
        diagnostics::print_error(
            "mqb plan currently supports ordinary C/C++ targets only; Modules/Header Units remain fail-closed");
        return 2;
    }
    if (options.build.target_kind == mqb::TargetKind::static_library) {
        diagnostics::print_error(
            "mqb plan static-library inspection is intentionally deferred to the librarian planning slice");
        return 2;
    }

    add_portable_root_if_missing(options.portable_roots, project_root / "portable_msvc");
    for (const auto& source : sources) {
        add_portable_root_if_missing(options.portable_roots, source.parent_path() / "portable_msvc");
    }

    mqb::platform::windows::WindowsProcessRunner runner;
    mqb::msvc::MsvcToolchainLocator locator{runner};
    mqb::msvc::DiscoveryOptions discovery;
    discovery.target_architecture = options.build.architecture;
    discovery.host_architecture = mqb::Architecture::x64;
    discovery.preference = options.toolchain_preference;
    discovery.portable_roots = options.portable_roots;
    std::string toolchain_cache_name = "vs-";
    toolchain_cache_name += mqb::to_string(options.build.architecture);
    toolchain_cache_name += ".cache";
    discovery.cache_file = layout->artifact_root() / "cache" / "toolchain" / toolchain_cache_name;
    auto toolchain = locator.discover(discovery);
    if (!toolchain) {
        diagnostics::print_error(toolchain.error().message);
        return 3;
    }
    if (!validate_capabilities(mqb::msvc::ParameterTool::compiler,
            options.compiler_arguments, toolchain->identity.version)
        || !validate_capabilities(mqb::msvc::ParameterTool::linker,
            options.linker_arguments, toolchain->identity.version)) {
        return 2;
    }
    if (!options.librarian_arguments.empty()) {
        diagnostics::print_error("mqb plan does not accept librarian policy for executable/DLL targets");
        return 2;
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

    const std::string target_name = options.build.output_name.value_or(
        diagnostics::path_text(requested_sources.front().stem()));
    auto target_artifacts = layout->for_target(target_name, options.build.target_kind);
    if (!target_artifacts) {
        diagnostics::print_error(target_artifacts.error().message);
        return 2;
    }

    mqb::msvc::MsvcCompileExecutor executor{*toolchain, runner};
    mqb::orchestration::MsvcIncrementalCompileCoordinator compile_coordinator{*toolchain, executor};
    std::vector<PlanPhase> phases;
    bool pch_rebuild = false;
    std::optional<mqb::PrecompiledHeaderArtifacts> pch_artifacts;

    if (effective.precompiled_header) {
        const auto c_source = std::find_if(target_sources.begin(), target_sources.end(),
            [](const auto& source) { return mqb::is_c_translation_unit_path(source.source); });
        if (c_source != target_sources.end()) {
            diagnostics::print_error("first-class PCH currently requires an ordinary C++ source set");
            return 2;
        }
        auto allocated = layout->for_precompiled_header(
            target_name, options.build.configuration, options.build.architecture);
        if (!allocated) {
            diagnostics::print_error(allocated.error().message);
            return 2;
        }
        pch_artifacts = std::move(*allocated);
        mqb::orchestration::MsvcIncrementalPchCoordinator pch_coordinator{compile_coordinator};
        auto inspected = pch_coordinator.inspect({
            .header = *effective.precompiled_header,
            .artifacts = *pch_artifacts,
            .compiler_options = compiler_options,
            .working_directory = project_root,
        });
        if (!inspected) {
            diagnostics::print_error(inspected.error().message);
            return 4;
        }
        auto recipe = executor.build_recipe(inspected->compile_request);
        if (!recipe) {
            diagnostics::print_error("failed to model PCH recipe: " + recipe.error().message);
            return 2;
        }
        pch_rebuild = !inspected->compile.plan.empty();
        phases.push_back(PlanPhase{
            .kind = "pch",
            .label = diagnostics::path_text(*effective.precompiled_header),
            .output = pch_artifacts->precompiled_header,
            .build = pch_rebuild,
            .reasons = inspected->compile.validation.reasons,
            .process = std::move(recipe->process),
        });
        compiler_options.precompiled_header = mqb::PrecompiledHeaderBinding{
            .header = effective.precompiled_header->lexically_normal(),
            .artifact = pch_artifacts->precompiled_header.lexically_normal(),
            .role = mqb::PrecompiledHeaderRole::use,
        };
    }

    bool any_compile = pch_rebuild;
    std::vector<fs::path> objects;
    if (pch_artifacts) objects.push_back(pch_artifacts->object);
    for (const auto& source : target_sources) {
        auto request = compile_request_for(source, compiler_options, project_root, pch_rebuild);
        auto inspected = compile_coordinator.inspect(request);
        if (!inspected) {
            diagnostics::print_error(inspected.error().message);
            return 4;
        }
        const mqb::msvc::CompileExecutionRequest execution_request{
            .unit = request.unit,
            .options = request.options,
            .source_dependencies_file = request.source_dependencies_file,
            .working_directory = request.working_directory,
        };
        auto recipe = executor.build_recipe(execution_request);
        if (!recipe) {
            diagnostics::print_error("failed to model compile recipe: " + recipe.error().message);
            return 2;
        }
        const bool rebuild = !inspected->plan.empty();
        any_compile = any_compile || rebuild;
        phases.push_back(PlanPhase{
            .kind = "compile",
            .label = diagnostics::path_text(source.source.lexically_relative(project_root)),
            .output = source.artifacts.object,
            .build = rebuild,
            .reasons = inspected->validation.reasons,
            .process = std::move(recipe->process),
        });
        objects.push_back(source.artifacts.object);
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
    mqb::msvc::MsvcAddressSanitizerPolicy::apply_link_policy(compiler_options, link_options);
    mqb::msvc::MsvcFuzzerPolicy::apply_link_policy(compiler_options, link_options);
    mqb::msvc::MsvcOpenMpPolicy::apply_link_policy(compiler_options, link_options);

    mqb::msvc::MsvcLinker linker{*toolchain, runner};
    mqb::orchestration::MsvcIncrementalLinkCoordinator link_coordinator{*toolchain, linker};
    mqb::orchestration::IncrementalLinkRequest link_request{
        .objects = std::move(objects),
        .output = target_artifacts->executable,
        .options = link_options,
        .cache_file = target_artifacts->link_cache,
        .working_directory = project_root,
        .force_relink = any_compile,
    };
    auto link = link_coordinator.inspect(link_request);
    if (!link) {
        diagnostics::print_error(link.error().message);
        return 5;
    }
    phases.push_back(PlanPhase{
        .kind = "link",
        .label = diagnostics::path_text(target_artifacts->executable.filename()),
        .output = target_artifacts->executable,
        .build = !link->plan.empty(),
        .reasons = link->validation.reasons,
        .process = std::nullopt,
    });

    if (parsed->format == PlanFormat::json) render_json(project_root, target_name, phases);
    else render_text(project_root, target_name, phases);
    return 0;
}

std::string_view plan_usage() noexcept {
    return R"(Usage:
  mqb plan [entry.cpp] [options] [MSVC-options]
  mqb plan [source...] [options] [MSVC-options]

Inspect the build MQB would perform without compiling, linking, archiving, or running.
The plan uses the same incremental cache/freshness authorities and exact MSVC compile recipes
as a real build.

Plan options:
  --format text|json      Output format (default: text)

First release scope: ordinary executable/DLL C/C++ targets, including first-class PCH.
Modules/Header Units and static-library planning fail closed until their dedicated graph/librarian
planning slices are added.
)";
}

} // namespace mqb::app
