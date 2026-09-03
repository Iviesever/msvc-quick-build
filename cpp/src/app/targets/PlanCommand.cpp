#include "PlanCommand.hpp"

#include <algorithm>
#include <expected>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "BuildIntrospectionSetup.hpp"
#include "Cli.hpp"
#include "Diagnostics.hpp"
#include "mqb/core/Artifact.hpp"
#include "mqb/core/BuildAction.hpp"
#include "mqb/core/BuildTypes.hpp"
#include "mqb/core/TranslationUnitClassifier.hpp"
#include "mqb/msvc/MsvcAddressSanitizerPolicy.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcFuzzerPolicy.hpp"
#include "mqb/msvc/MsvcLinker.hpp"
#include "mqb/msvc/MsvcOpenMpPolicy.hpp"
#include "mqb/msvc/MsvcParameterCapabilities.hpp"
#include "mqb/msvc/MsvcParameterEngine.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalPchCoordinator.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"

namespace mqb::app {
namespace {

namespace fs = std::filesystem;

enum class PlanFormat {
    text,
    json,
};

struct ParsedPlanOptions {
    mqb::cli::Options build;
    PlanFormat format{PlanFormat::text};
};

struct PlanStep {
    std::string kind;
    std::string label;
    bool planned{false};
    std::vector<mqb::BuildReason> reasons;
    std::vector<fs::path> outputs;
    std::optional<mqb::process::ProcessSpec> process;
};

[[nodiscard]] std::string path_to_utf8(const fs::path& path) {
    const auto bytes = path.lexically_normal().generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()};
}

[[nodiscard]] fs::path absolute_from(
    const fs::path& base,
    const fs::path& path) {
    return path.is_absolute()
        ? path.lexically_normal()
        : (base / path).lexically_normal();
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
                escaped << "\\u00"
                        << std::setw(2) << std::setfill('0')
                        << static_cast<unsigned int>(ch);
            } else {
                escaped << static_cast<char>(ch);
            }
            break;
        }
    }
    return escaped.str();
}

[[nodiscard]] std::expected<PlanFormat, std::string>
parse_format(const std::string_view value) {
    if (value == "text") return PlanFormat::text;
    if (value == "json") return PlanFormat::json;
    return std::unexpected("mqb plan --format must be 'text' or 'json'");
}

[[nodiscard]] std::expected<ParsedPlanOptions, std::string>
parse_plan_arguments(const std::span<const std::string_view> arguments) {
    std::vector<std::string_view> forwarded;
    forwarded.reserve(arguments.size() + 1u);
    forwarded.emplace_back("build");

    PlanFormat format = PlanFormat::text;
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
                return std::unexpected("mqb plan --format may be specified only once");
            }
            if (index + 1 >= arguments.size()) {
                return std::unexpected("mqb plan --format requires text or json");
            }
            auto parsed = parse_format(arguments[++index]);
            if (!parsed) return std::unexpected(parsed.error());
            format = *parsed;
            format_seen = true;
            continue;
        }
        if (!native_tail && argument.starts_with("--format=")) {
            if (format_seen) {
                return std::unexpected("mqb plan --format may be specified only once");
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
validate_linker_capabilities(
    const std::vector<std::string>& arguments,
    const std::string_view vc_tools_version) {
    for (const auto& argument : arguments) {
        const auto capability = mqb::msvc::MsvcParameterCapabilities::inspect(
            mqb::msvc::ParameterTool::linker,
            argument,
            vc_tools_version);
        if (capability.lifecycle == mqb::msvc::ParameterLifecycle::active) continue;

        std::string message = "MSVC linker option '" + argument + "' is ";
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

void append_step_outputs(
    PlanStep& step,
    const mqb::TranslationUnit& unit) {
    step.outputs.reserve(unit.outputs.size());
    for (const auto& output : unit.outputs) {
        step.outputs.push_back(output.path);
    }
}

[[nodiscard]] std::string display_path(
    const fs::path& project_root,
    const fs::path& path) {
    const fs::path relative = path.lexically_relative(project_root);
    if (!relative.empty() && !relative.is_absolute()) {
        bool safe = true;
        for (const auto& component : relative) {
            if (component == "..") {
                safe = false;
                break;
            }
        }
        if (safe) return path_to_utf8(relative);
    }
    return path_to_utf8(path);
}

[[nodiscard]] std::string render_text(
    const BuildIntrospectionContext& context,
    const std::vector<PlanStep>& steps) {
    std::ostringstream output;
    output << "MQB build plan\n"
           << "project: " << path_to_utf8(context.project.project_root) << '\n'
           << "target:  " << context.target_name << " ("
           << mqb::to_string(context.options.build.target_kind) << ")\n"
           << "output:  " << path_to_utf8(context.target_artifacts.executable) << '\n'
           << "steps:\n";

    std::size_t planned = 0;
    for (const auto& step : steps) {
        if (step.planned) ++planned;
        output << "  [" << (step.planned ? "plan" : "up-to-date") << "] "
               << step.kind << ' ' << step.label << '\n';
        if (!step.reasons.empty()) {
            output << "    reasons: ";
            for (std::size_t index = 0; index < step.reasons.size(); ++index) {
                if (index != 0) output << ", ";
                output << mqb::to_string(step.reasons[index]);
            }
            output << '\n';
        }
        for (const auto& artifact : step.outputs) {
            output << "    output: "
                   << display_path(context.project.project_root, artifact) << '\n';
        }
        if (step.process) {
            output << "    executable: " << path_to_utf8(step.process->executable) << '\n';
            if (step.process->working_directory) {
                output << "    directory:  "
                       << path_to_utf8(*step.process->working_directory) << '\n';
            }
            output << "    argv:";
            for (const auto& argument : step.process->arguments) {
                output << "\n      " << argument;
            }
            output << '\n';
        }
    }
    output << "summary: " << planned << " planned, "
           << (steps.size() - planned) << " up-to-date\n";
    return output.str();
}

void render_json_process(
    std::ostringstream& output,
    const mqb::process::ProcessSpec& process,
    const std::string_view indent) {
    output << indent << "\"process\": {\n"
           << indent << "  \"executable\": \""
           << json_escape(path_to_utf8(process.executable)) << "\",\n"
           << indent << "  \"arguments\": [";
    for (std::size_t index = 0; index < process.arguments.size(); ++index) {
        if (index != 0) output << ", ";
        output << "\"" << json_escape(process.arguments[index]) << "\"";
    }
    output << "],\n"
           << indent << "  \"working_directory\": ";
    if (process.working_directory) {
        output << "\"" << json_escape(path_to_utf8(*process.working_directory)) << "\"";
    } else {
        output << "null";
    }
    output << ",\n"
           << indent << "  \"inherit_environment\": "
           << (process.inherit_environment ? "true" : "false") << ",\n"
           << indent << "  \"environment\": [";
    for (std::size_t index = 0; index < process.environment.size(); ++index) {
        if (index != 0) output << ", ";
        const auto& variable = process.environment[index];
        output << "{\"name\":\"" << json_escape(variable.name)
               << "\",\"value\":\"" << json_escape(variable.value)
               << "\",\"remove\":" << (variable.remove ? "true" : "false") << '}';
    }
    output << "]\n" << indent << '}';
}

[[nodiscard]] std::string render_json(
    const BuildIntrospectionContext& context,
    const std::vector<PlanStep>& steps) {
    std::size_t planned = 0;
    for (const auto& step : steps) {
        if (step.planned) ++planned;
    }

    std::ostringstream output;
    output << "{\n"
           << "  \"version\": 1,\n"
           << "  \"project\": \""
           << json_escape(path_to_utf8(context.project.project_root)) << "\",\n"
           << "  \"target\": {\"name\": \"" << json_escape(context.target_name)
           << "\", \"type\": \"" << json_escape(mqb::to_string(context.options.build.target_kind))
           << "\", \"output\": \""
           << json_escape(path_to_utf8(context.target_artifacts.executable)) << "\"},\n"
           << "  \"toolchain\": {\"compiler\": \""
           << json_escape(path_to_utf8(context.toolchain.identity.compiler))
           << "\", \"linker\": \"" << json_escape(path_to_utf8(context.toolchain.linker))
           << "\", \"version\": \"" << json_escape(context.toolchain.identity.version)
           << "\"},\n"
           << "  \"steps\": [\n";

    for (std::size_t step_index = 0; step_index < steps.size(); ++step_index) {
        const auto& step = steps[step_index];
        output << "    {\n"
               << "      \"kind\": \"" << json_escape(step.kind) << "\",\n"
               << "      \"label\": \"" << json_escape(step.label) << "\",\n"
               << "      \"status\": \"" << (step.planned ? "planned" : "up_to_date") << "\",\n"
               << "      \"reasons\": [";
        for (std::size_t index = 0; index < step.reasons.size(); ++index) {
            if (index != 0) output << ", ";
            output << "\"" << json_escape(mqb::to_string(step.reasons[index])) << "\"";
        }
        output << "],\n      \"outputs\": [";
        for (std::size_t index = 0; index < step.outputs.size(); ++index) {
            if (index != 0) output << ", ";
            output << "\"" << json_escape(path_to_utf8(step.outputs[index])) << "\"";
        }
        output << ']';
        if (step.process) {
            output << ",\n";
            render_json_process(output, *step.process, "      ");
            output << '\n';
        } else {
            output << '\n';
        }
        output << "    }";
        if (step_index + 1 != steps.size()) output << ',';
        output << '\n';
    }

    output << "  ],\n"
           << "  \"summary\": {\"planned\": " << planned
           << ", \"up_to_date\": " << (steps.size() - planned) << "}\n"
           << "}\n";
    return output.str();
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
        diagnostics::print_error("mqb plan does not accept --run or program arguments");
        return 2;
    }
    if (context->module_target) {
        diagnostics::print_error(
            "mqb plan currently supports ordinary C/C++ targets only; "
            "Modules/Header Units require graph-aware inspection and are intentionally fail-closed");
        return 2;
    }
    if (context->options.build.target_kind == mqb::TargetKind::static_library) {
        diagnostics::print_error(
            "mqb plan does not yet expose static-library execution recipes; "
            "lib.exe uses an execution-time transactional temporary archive path, so exact argv is intentionally fail-closed");
        return 2;
    }
    if (!context->options.librarian_arguments.empty()) {
        diagnostics::print_error(
            "native MSVC librarian policy is only valid for static-library targets");
        return 2;
    }

    auto linker_capabilities = validate_linker_capabilities(
        context->options.linker_arguments,
        context->toolchain.identity.version);
    if (!linker_capabilities) {
        diagnostics::print_error(linker_capabilities.error());
        return 2;
    }

    mqb::msvc::MsvcCompileExecutor compile_executor{context->toolchain, runner};
    mqb::orchestration::MsvcIncrementalCompileCoordinator compile_coordinator{
        context->toolchain,
        compile_executor};

    std::vector<PlanStep> steps;
    steps.reserve(context->target_sources.size() + (context->pch_artifacts ? 2u : 1u));

    bool pch_planned = false;
    if (context->pch_artifacts && context->project.effective.precompiled_header) {
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
        PlanStep step{
            .kind = "pch",
            .label = display_path(
                context->project.project_root,
                *context->project.effective.precompiled_header),
            .planned = pch_planned,
            .reasons = inspected->compile.validation.reasons,
        };
        append_step_outputs(step, inspected->compile_request.unit);
        if (pch_planned) {
            auto recipe = compile_executor.build_recipe(inspected->compile_request);
            if (!recipe) {
                diagnostics::print_error(
                    "failed to model PCH creator recipe: " + recipe.error().message);
                return 4;
            }
            step.process = std::move(recipe->process);
        }
        steps.push_back(std::move(step));
    }

    const mqb::CompilerOptions consumer_options = context->consumer_compiler_options();
    std::vector<fs::path> objects;
    objects.reserve(context->target_sources.size() + (context->pch_artifacts ? 1u : 0u));
    if (context->pch_artifacts) {
        objects.push_back(context->pch_artifacts->object);
    }

    bool any_compile_planned = false;
    for (const auto& source : context->target_sources) {
        const auto kind = mqb::classify_translation_unit_path(source.source);
        if (!kind || *kind != mqb::TranslationUnitKind::source) {
            diagnostics::print_error(
                "mqb plan encountered a non-ordinary translation unit after ordinary-target validation");
            return 2;
        }

        mqb::TranslationUnit unit;
        unit.source = source.source;
        unit.kind = *kind;
        unit.outputs = {
            mqb::Artifact{source.artifacts.object, mqb::ArtifactKind::object},
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
        PlanStep step{
            .kind = "compile",
            .label = display_path(context->project.project_root, source.source),
            .planned = planned,
            .reasons = inspected->validation.reasons,
        };
        append_step_outputs(step, unit);
        if (planned) {
            auto recipe = compile_executor.build_recipe(
                mqb::msvc::CompileExecutionRequest{
                    .unit = unit,
                    .options = consumer_options,
                    .source_dependencies_file = source.artifacts.dependencies,
                    .working_directory = source.source.parent_path(),
                });
            if (!recipe) {
                diagnostics::print_error(
                    "failed to model compile recipe: " + recipe.error().message);
                return 4;
            }
            step.process = std::move(recipe->process);
        }
        steps.push_back(std::move(step));
        objects.push_back(source.artifacts.object);
    }

    mqb::LinkOptions link_options;
    link_options.configuration = context->options.build.configuration;
    link_options.architecture = context->options.build.architecture;
    link_options.target_kind = context->options.build.target_kind;
    link_options.subsystem = context->options.subsystem_override.value_or(
        mqb::LinkSubsystem::console);
    link_options.link_time_code_generation = context->project.effective.link_time_code_generation;
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
    auto link_inspection = link_coordinator.inspect(link_request);
    if (!link_inspection) {
        diagnostics::print_error(link_inspection.error().message);
        return 5;
    }

    const bool link_planned = !link_inspection->plan.empty();
    PlanStep link_step{
        .kind = "link",
        .label = display_path(
            context->project.project_root,
            context->target_artifacts.executable),
        .planned = link_planned,
        .reasons = link_inspection->validation.reasons,
        .outputs = {context->target_artifacts.executable},
    };
    if (link_planned) {
        if (link_inspection->plan.actions.size() != 1) {
            diagnostics::print_error("mqb plan expected exactly one link action");
            return 5;
        }
        const auto* action = std::get_if<mqb::LinkAction>(
            &link_inspection->plan.actions.front());
        if (action == nullptr) {
            diagnostics::print_error("mqb plan received a non-link action from link inspection");
            return 5;
        }
        auto linker_file_routing = mqb::msvc::MsvcParameterEngine::linker_file_inputs(
            link_options.additional_arguments,
            context->project.project_root);
        if (!linker_file_routing) {
            diagnostics::print_error(linker_file_routing.error().message);
            return 5;
        }
        const mqb::msvc::LinkInvocation invocation{
            .objects = action->objects,
            .output = action->output,
            .libraries = action->libraries,
            .options = link_options,
            .working_directory = context->project.project_root,
            .force_full_link = link_inspection->validation.library_inputs_changed
                || link_inspection->validation.file_inputs_changed
                || linker_file_routing->requires_full_link
                || link_options.address_sanitizer_runtime_library.has_value(),
            .observe_library_search = true,
        };
        auto recipe = mqb::msvc::MsvcLinker::build_recipe(
            context->toolchain,
            invocation);
        if (!recipe) {
            diagnostics::print_error(
                "failed to model link recipe: " + recipe.error().message);
            return 5;
        }
        link_step.process = std::move(recipe->process);
    }
    steps.push_back(std::move(link_step));

    if (parsed->format == PlanFormat::json) {
        std::cout << render_json(*context, steps);
    } else {
        std::cout << render_text(*context, steps);
    }
    return 0;
}

std::string_view plan_usage() noexcept {
    return R"(Usage:
  mqb plan [entry.cpp] [options] [MSVC-compiler-options] [/link linker-options...]
  mqb plan [source...] [options] [MSVC-compiler-options] [/link linker-options...]

Inspect the exact MQB incremental decision without compiling, linking, archiving,
or mutating project .mqb state. The command reports cache hit/miss decisions,
rebuild reasons, planned artifacts, and exact cl.exe/link.exe process recipes.

Plan-only options:
  --format <text|json>     Output format (default: text)

Other options follow `mqb build`. The current slice supports ordinary executable
and DLL targets, including first-class PCH creators/consumers. Modules/Header Units
and static-library execution recipes fail closed instead of emitting approximate plans.
)";
}

} // namespace mqb::app
