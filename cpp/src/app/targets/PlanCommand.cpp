#include "PlanCommand.hpp"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "BuildIntrospectionSetup.hpp"
#include "Cli.hpp"
#include "Diagnostics.hpp"
#include "ModulePlanCommand.hpp"
#include "PlanOutput.hpp"
#include "PlanSupport.hpp"
#include "mqb/core/Artifact.hpp"
#include "mqb/core/BuildTypes.hpp"
#include "mqb/core/TranslationUnitClassifier.hpp"
#include "mqb/msvc/MsvcAddressSanitizerPolicy.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcFuzzerPolicy.hpp"
#include "mqb/msvc/MsvcLibrarian.hpp"
#include "mqb/msvc/MsvcLinker.hpp"
#include "mqb/msvc/MsvcOpenMpPolicy.hpp"
#include "mqb/msvc/MsvcParameterCapabilities.hpp"
#include "mqb/orchestration/MsvcIncrementalArchiveCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalPchCoordinator.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"

namespace mqb::app {
namespace {

namespace fs = std::filesystem;

struct ParsedPlanOptions {
    mqb::cli::Options build;
    plan::Format format{plan::Format::text};
};

[[nodiscard]] std::expected<plan::Format, std::string>
parse_format(const std::string_view value) {
    if (value == "text") return plan::Format::text;
    if (value == "json") return plan::Format::json;
    return std::unexpected("mqb plan --format must be 'text' or 'json'");
}

[[nodiscard]] std::expected<ParsedPlanOptions, std::string>
parse_plan_arguments(const std::span<const std::string_view> arguments) {
    std::vector<std::string_view> forwarded;
    forwarded.reserve(arguments.size() + 1u);
    forwarded.emplace_back("build");

    plan::Format format = plan::Format::text;
    bool format_seen = false;
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
            if (format_seen) {
                return std::unexpected(
                    "mqb plan --format may be specified only once");
            }
            if (index + 1 >= arguments.size()) {
                return std::unexpected(
                    "mqb plan --format requires text or json");
            }
            auto parsed = parse_format(arguments[++index]);
            if (!parsed) return std::unexpected(parsed.error());
            format = *parsed;
            format_seen = true;
            continue;
        }
        if (!native_tail && argument.starts_with("--format=")) {
            if (format_seen) {
                return std::unexpected(
                    "mqb plan --format may be specified only once");
            }
            auto parsed = parse_format(
                argument.substr(std::string_view{"--format="}.size()));
            if (!parsed) return std::unexpected(parsed.error());
            format = *parsed;
            format_seen = true;
            continue;
        }
        forwarded.push_back(argument);
    }

    auto parsed = mqb::cli::parse_arguments(forwarded);
    if (!parsed) return std::unexpected(parsed.error().message);
    return ParsedPlanOptions{
        .build = std::move(*parsed),
        .format = format,
    };
}

int print_setup_error(const BuildIntrospectionError& error) {
    if (error.config_error) {
        diagnostics::print_config_error(*error.config_error);
    } else {
        diagnostics::print_error(error.message);
    }
    return error.exit_code;
}

[[nodiscard]] std::expected<void, std::string>
validate_tool_capabilities(
    const mqb::msvc::ParameterTool tool,
    const std::vector<std::string>& arguments,
    const std::string_view vc_tools_version) {
    for (const auto& argument : arguments) {
        const auto capability = mqb::msvc::MsvcParameterCapabilities::inspect(
            tool,
            argument,
            vc_tools_version);
        if (capability.lifecycle == mqb::msvc::ParameterLifecycle::active) {
            continue;
        }

        std::string message = "MSVC ";
        message += mqb::msvc::to_string(tool);
        message += " option '" + argument + "' is ";
        message += mqb::msvc::to_string(capability.lifecycle);
        message += " for toolset ";
        message += vc_tools_version;
        if (!capability.guidance.empty()) {
            message += ": ";
            message += capability.guidance;
        }
        if (capability.lifecycle == mqb::msvc::ParameterLifecycle::deprecated) {
            diagnostics::print_warning(message);
            continue;
        }
        return std::unexpected(std::move(message));
    }
    return {};
}

} // namespace

int run_plan_command(const std::span<const std::string_view> arguments) {
    auto parsed = parse_plan_arguments(arguments);
    if (!parsed) {
        diagnostics::print_error(parsed.error());
        return 2;
    }
    if (parsed->build.show_help) {
        std::cout << plan_usage();
        return 0;
    }

    mqb::platform::windows::WindowsProcessRunner runner;
    auto context = prepare_build_introspection(
        std::move(parsed->build),
        runner,
        BuildIntrospectionPolicy{
            .persistent_discovery_cache = false,
            .persistent_toolchain_cache = false,
        });
    if (!context) return print_setup_error(context.error());

    if (context->options.build.run_after_build
        || !context->options.build.run_arguments.empty()) {
        diagnostics::print_error(
            "mqb plan does not accept --run or program arguments");
        return 2;
    }

    const bool static_target =
        context->options.build.target_kind == mqb::TargetKind::static_library;
    if (static_target) {
        if (context->project.subsystem_explicit
            || !context->options.library_directories.empty()
            || !context->options.libraries.empty()
            || !context->options.linker_arguments.empty()) {
            diagnostics::print_error(
                "static-library targets do not accept linker-only policy "
                "(subsystem, library paths/libraries, or linker args)");
            return 2;
        }
        auto librarian_capabilities = validate_tool_capabilities(
            mqb::msvc::ParameterTool::librarian,
            context->options.librarian_arguments,
            context->toolchain.identity.version);
        if (!librarian_capabilities) {
            diagnostics::print_error(librarian_capabilities.error());
            return 2;
        }
    } else {
        if (!context->options.librarian_arguments.empty()) {
            diagnostics::print_error(
                "native MSVC librarian policy is only valid for static-library targets");
            return 2;
        }
        auto linker_capabilities = validate_tool_capabilities(
            mqb::msvc::ParameterTool::linker,
            context->options.linker_arguments,
            context->toolchain.identity.version);
        if (!linker_capabilities) {
            diagnostics::print_error(linker_capabilities.error());
            return 2;
        }
    }

    if (context->module_target) {
        return run_module_plan(parsed->format, *context, runner);
    }

    mqb::msvc::MsvcCompileExecutor compile_executor{
        context->toolchain,
        runner};
    mqb::orchestration::MsvcIncrementalCompileCoordinator compile_coordinator{
        context->toolchain,
        compile_executor};

    plan::Document document;
    document.steps.reserve(
        context->target_sources.size()
        + (context->pch_artifacts ? 2u : 1u));

    bool pch_planned = false;
    if (context->pch_artifacts
        && context->project.effective.precompiled_header) {
        mqb::orchestration::MsvcIncrementalPchCoordinator pch_coordinator{
            compile_coordinator};
        auto inspected = pch_coordinator.inspect(
            mqb::orchestration::IncrementalPchRequest{
                .header = *context->project.effective.precompiled_header,
                .artifacts = *context->pch_artifacts,
                .compiler_options = context->compiler_options,
                .working_directory = context->project.project_root,
            });
        if (!inspected) {
            diagnostics::print_error(inspected.error().message);
            return 4;
        }

        pch_planned = !inspected->compile.plan.empty();
        plan::Step step{
            .kind = "pch",
            .label = plan::display_path(
                context->project.project_root,
                *context->project.effective.precompiled_header),
            .planned = pch_planned,
            .reasons = plan::reason_texts(
                inspected->compile.validation.reasons),
        };
        plan::append_outputs(step, inspected->compile_request.unit);
        if (pch_planned) {
            auto recipe = compile_executor.build_recipe(
                inspected->compile_request);
            if (!recipe) {
                diagnostics::print_error(
                    "failed to model PCH creator recipe: "
                    + recipe.error().message);
                return 4;
            }
            step.process = std::move(recipe->process);
        }
        document.steps.push_back(std::move(step));
    }

    const mqb::CompilerOptions consumer_options =
        context->consumer_compiler_options();
    std::vector<fs::path> objects;
    objects.reserve(
        context->target_sources.size()
        + (context->pch_artifacts ? 1u : 0u));
    if (context->pch_artifacts) {
        objects.push_back(context->pch_artifacts->object);
    }

    bool any_compile_planned = false;
    for (const auto& source : context->target_sources) {
        const auto kind = mqb::classify_translation_unit_path(source.source);
        if (!kind || *kind != mqb::TranslationUnitKind::source) {
            diagnostics::print_error(
                "mqb plan encountered a non-ordinary translation unit "
                "after ordinary-target validation");
            return 2;
        }

        mqb::TranslationUnit unit;
        unit.source = source.source;
        unit.kind = *kind;
        unit.outputs = {
            mqb::Artifact{
                source.artifacts.object,
                mqb::ArtifactKind::object},
        };
        const mqb::orchestration::IncrementalCompileRequest compile_request{
            .unit = unit,
            .options = consumer_options,
            .cache_file = source.artifacts.compile_cache,
            .source_dependencies_file = source.artifacts.dependencies,
            .working_directory = source.source.parent_path(),
            .force_rebuild = pch_planned,
        };
        auto inspected = compile_coordinator.inspect(compile_request);
        if (!inspected) {
            diagnostics::print_error(inspected.error().message);
            return 4;
        }

        const bool planned = !inspected->plan.empty();
        any_compile_planned = any_compile_planned || planned;
        plan::Step step{
            .kind = "compile",
            .label = plan::display_path(
                context->project.project_root,
                source.source),
            .planned = planned,
            .reasons = plan::reason_texts(inspected->validation.reasons),
        };
        plan::append_outputs(step, unit);
        if (planned) {
            auto recipe = compile_executor.build_recipe(
                plan::execution_request_for(compile_request));
            if (!recipe) {
                diagnostics::print_error(
                    "failed to model compile recipe: "
                    + recipe.error().message);
                return 4;
            }
            step.process = std::move(recipe->process);
        }
        document.steps.push_back(std::move(step));
        objects.push_back(source.artifacts.object);
    }

    if (static_target) {
        mqb::msvc::MsvcLibrarian librarian{context->toolchain, runner};
        mqb::orchestration::MsvcIncrementalArchiveCoordinator archive_coordinator{
            context->toolchain,
            librarian};
        const mqb::orchestration::IncrementalArchiveRequest archive_request{
            .objects = objects,
            .output = context->target_artifacts.executable,
            .cache_file = context->target_artifacts.link_cache,
            .working_directory = context->project.project_root,
            .architecture = context->options.build.architecture,
            .link_time_code_generation =
                context->project.effective.link_time_code_generation,
            .additional_arguments = context->options.librarian_arguments,
            .force_archive = pch_planned || any_compile_planned,
        };
        auto inspected = archive_coordinator.inspect(archive_request);
        if (!inspected) {
            diagnostics::print_error(inspected.error().message);
            return 5;
        }

        const bool planned = !inspected->plan.empty();
        plan::Step step{
            .kind = "archive",
            .label = plan::display_path(
                context->project.project_root,
                context->target_artifacts.executable),
            .planned = planned,
            .reasons = plan::reason_texts(inspected->validation.reasons),
            .outputs = {context->target_artifacts.executable},
        };
        if (planned) {
            if (!inspected->invocation) {
                diagnostics::print_error(
                    "mqb plan received an archive action without its typed "
                    "librarian invocation");
                return 5;
            }
            auto recipe = mqb::msvc::MsvcLibrarian::build_recipe(
                context->toolchain,
                *inspected->invocation);
            if (!recipe) {
                diagnostics::print_error(
                    "failed to model archive recipe: "
                    + recipe.error().message);
                return 5;
            }
            step.process = std::move(recipe->process);
        }
        document.steps.push_back(std::move(step));
        plan::render(parsed->format, *context, document);
        return 0;
    }

    mqb::LinkOptions link_options;
    link_options.configuration = context->options.build.configuration;
    link_options.architecture = context->options.build.architecture;
    link_options.target_kind = context->options.build.target_kind;
    link_options.subsystem = context->options.subsystem_override.value_or(
        mqb::LinkSubsystem::console);
    link_options.link_time_code_generation =
        context->project.effective.link_time_code_generation;
    link_options.library_directories = context->options.library_directories;
    link_options.libraries = context->options.libraries;
    link_options.additional_arguments = context->options.linker_arguments;
    mqb::msvc::MsvcAddressSanitizerPolicy::apply_link_policy(
        consumer_options,
        link_options);
    mqb::msvc::MsvcFuzzerPolicy::apply_link_policy(
        consumer_options,
        link_options);
    mqb::msvc::MsvcOpenMpPolicy::apply_link_policy(
        consumer_options,
        link_options);

    mqb::msvc::MsvcLinker linker{context->toolchain, runner};
    mqb::orchestration::MsvcIncrementalLinkCoordinator link_coordinator{
        context->toolchain,
        linker};
    const mqb::orchestration::IncrementalLinkRequest link_request{
        .objects = objects,
        .output = context->target_artifacts.executable,
        .options = link_options,
        .cache_file = context->target_artifacts.link_cache,
        .working_directory = context->project.project_root,
        .force_relink = pch_planned || any_compile_planned,
    };
    auto inspected = link_coordinator.inspect(link_request);
    if (!inspected) {
        diagnostics::print_error(inspected.error().message);
        return 5;
    }

    const bool planned = !inspected->plan.empty();
    plan::Step step{
        .kind = "link",
        .label = plan::display_path(
            context->project.project_root,
            context->target_artifacts.executable),
        .planned = planned,
        .reasons = plan::reason_texts(inspected->validation.reasons),
        .outputs = {context->target_artifacts.executable},
    };
    if (planned) {
        auto process = plan::model_link_process(
            context->toolchain,
            link_request,
            *inspected);
        if (!process) {
            diagnostics::print_error(process.error());
            return 5;
        }
        step.process = std::move(*process);
    }
    document.steps.push_back(std::move(step));

    plan::render(parsed->format, *context, document);
    return 0;
}

std::string_view plan_usage() noexcept {
    return R"(Usage:
  mqb plan [entry.cpp] [options] [MSVC-compiler-options] [/link linker-options...]
  mqb plan [source...] [options] [MSVC-compiler-options] [/lib librarian-options...]

Inspect the exact MQB incremental decision without compiling, scanning, linking,
archiving, or mutating project .mqb state. The command reports cache decisions,
rebuild reasons, planned artifacts, and exact MSVC process recipes.

Plan-only options:
  --format <text|json>     Output format (default: text)

Other options follow `mqb build`. Executable and DLL Modules/Header Units targets
are graph-aware: cold or stale P1689 evidence reports exact scan steps with a
pending graph, while trustworthy warm evidence continues through provider graph,
compile levels, exact /reference or /headerUnit recipes, and final link planning.
Ordinary C/C++, first-class PCH, and static-library targets remain supported.
Static-library targets that require the Modules/Header Units pipeline still fail
closed because that product combination is not yet implemented.
)";
}

} // namespace mqb::app
