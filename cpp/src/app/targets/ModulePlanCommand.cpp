#include "ModulePlanCommand.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Diagnostics.hpp"
#include "PlanSupport.hpp"
#include "mqb/core/TranslationUnitClassifier.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcLinker.hpp"
#include "mqb/msvc/MsvcModuleDependencyScanner.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"
#include "mqb/orchestration/MsvcModuleCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcModuleTargetCoordinator.hpp"
#include "mqb/orchestration/ParallelismPolicy.hpp"
#include "mqb/platform/windows/PathIdentity.hpp"

namespace mqb::app {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] bool same_path(const fs::path& left, const fs::path& right) {
    return mqb::platform::windows::path_identity_key(left)
        == mqb::platform::windows::path_identity_key(right);
}

[[nodiscard]] bool requested_project_source(
    const BuildIntrospectionContext& context,
    const fs::path& source) {
    return std::any_of(
        context.target_sources.begin(),
        context.target_sources.end(),
        [&](const mqb::orchestration::TargetSourceRequest& candidate) {
            return same_path(candidate.source, source);
        });
}

[[nodiscard]] std::string owner_for_source(
    const BuildIntrospectionContext& context,
    const fs::path& source) {
    return requested_project_source(context, source) ? "project" : "toolchain";
}

[[nodiscard]] std::string role_for_unit(const mqb::TranslationUnit& unit) {
    if (unit.header_unit) return "header_unit";
    if (unit.kind == mqb::TranslationUnitKind::module_interface) {
        return "module_interface";
    }
    return "translation_unit";
}

[[nodiscard]] mqb::LinkOptions make_link_options(
    const BuildIntrospectionContext& context) {
    mqb::LinkOptions options;
    options.configuration = context.options.build.configuration;
    options.architecture = context.options.build.architecture;
    options.target_kind = context.options.build.target_kind;
    options.subsystem = context.options.subsystem_override.value_or(
        mqb::LinkSubsystem::console);
    options.link_time_code_generation =
        context.project.effective.link_time_code_generation;
    options.library_directories = context.options.library_directories;
    options.libraries = context.options.libraries;
    options.additional_arguments = context.options.linker_arguments;
    return options;
}

[[nodiscard]] const mqb::orchestration::ModuleCompileInspection*
find_compile_inspection(
    const mqb::orchestration::ModuleCompileWaveInspection& inspection,
    const fs::path& source) {
    const auto found = std::find_if(
        inspection.compiles.begin(),
        inspection.compiles.end(),
        [&](const mqb::orchestration::ModuleCompileInspection& candidate) {
            return same_path(candidate.source, source);
        });
    return found == inspection.compiles.end() ? nullptr : &*found;
}

[[nodiscard]] const mqb::orchestration::HeaderUnitCompileInspection*
find_header_unit_inspection(
    const mqb::orchestration::ModuleCompileWaveInspection& inspection,
    const fs::path& source) {
    const auto found = std::find_if(
        inspection.header_unit_compiles.begin(),
        inspection.header_unit_compiles.end(),
        [&](const mqb::orchestration::HeaderUnitCompileInspection& candidate) {
            return same_path(candidate.source, source);
        });
    return found == inspection.header_unit_compiles.end()
        ? nullptr
        : &*found;
}

[[nodiscard]] int print_module_inspection_error(
    const mqb::orchestration::IncrementalModuleTargetError& error) {
    diagnostics::print_module_target_failure(error);
    return error.code
            == mqb::orchestration::IncrementalModuleTargetErrorCode::link_failed
        ? 5
        : 4;
}

[[nodiscard]] std::optional<mqb::process::ProcessSpec> model_compile_process(
    const mqb::orchestration::IncrementalCompileRequest& request,
    mqb::msvc::MsvcCompileExecutor& executor) {
    auto recipe = executor.build_recipe(plan::execution_request_for(request));
    if (!recipe) {
        diagnostics::print_error(
            "failed to model module compile recipe: " + recipe.error().message);
        return std::nullopt;
    }
    return std::move(recipe->process);
}

} // namespace

int run_module_plan(
    const plan::Format format,
    const BuildIntrospectionContext& context,
    mqb::platform::windows::WindowsProcessRunner& runner) {
    if (context.options.build.target_kind == mqb::TargetKind::static_library) {
        diagnostics::print_error(
            "static-library targets do not yet support the Modules/Header Unit pipeline");
        return 2;
    }

    std::vector<mqb::orchestration::ModuleCompileSourceRequest> sources;
    sources.reserve(context.target_sources.size());
    for (const auto& source : context.target_sources) {
        const auto kind = mqb::classify_translation_unit_path(source.source);
        if (!kind) {
            diagnostics::print_error(
                "unsupported translation unit in module build plan: "
                + diagnostics::path_text(source.source));
            return 2;
        }
        sources.push_back(mqb::orchestration::ModuleCompileSourceRequest{
            .source = source.source,
            .artifacts = source.artifacts,
            .kind = *kind,
        });
    }

    mqb::msvc::MsvcCompileExecutor compile_executor{context.toolchain, runner};
    mqb::orchestration::MsvcIncrementalCompileCoordinator incremental_compile{
        context.toolchain,
        compile_executor};
    mqb::orchestration::MsvcModuleCompileCoordinator module_compile{
        incremental_compile};
    mqb::msvc::MsvcLinker linker{context.toolchain, runner};
    mqb::orchestration::MsvcIncrementalLinkCoordinator incremental_link{
        context.toolchain,
        linker};
    mqb::msvc::MsvcModuleDependencyScanner scanner{context.toolchain, runner};
    mqb::orchestration::MsvcModuleTargetCoordinator target{
        scanner,
        module_compile,
        incremental_link};

    const mqb::orchestration::ParallelismPolicy parallelism =
        context.options.jobs.value_or(
            mqb::orchestration::ParallelismPolicy::automatic());
    const mqb::orchestration::IncrementalModuleTargetRequest request{
        .sources = std::move(sources),
        .target = context.target_artifacts,
        .artifact_layout = context.layout,
        .compiler_options = context.compiler_options,
        .link_options = make_link_options(context),
        .working_directory = context.project.project_root,
        .max_parallel_scans = parallelism,
        .max_parallel_compiles = parallelism,
    };

    auto inspected = target.inspect(request);
    if (!inspected) {
        return print_module_inspection_error(inspected.error());
    }

    plan::Document document;
    document.module_graph = plan::ModuleGraph{
        .ready = inspected->graph_ready(),
    };
    document.steps.reserve(
        inspected->scans.size()
        + (inspected->compiles
               ? inspected->compiles->compiles.size()
                   + inspected->compiles->header_unit_compiles.size()
                   + 1u
               : 0u));

    for (const auto& scan : inspected->scans) {
        plan::Step step{
            .kind = "module_scan",
            .label = plan::display_path(
                context.project.project_root,
                scan.source),
            .planned = scan.result.scan_required(),
            .reasons = plan::scan_reason_texts(scan.result.reasons),
            .outputs = {scan.result.recipe.invocation.output_file},
            .owner = scan.toolchain_owned ? "toolchain" : "project",
            .role = scan.result.recipe.invocation.kind
                    == mqb::TranslationUnitKind::module_interface
                ? "module_interface"
                : "translation_unit",
        };
        if (step.planned) {
            step.process = scan.result.recipe.process;
        }
        document.steps.push_back(std::move(step));
    }

    if (!inspected->graph_ready()) {
        plan::render(format, context, document);
        return 0;
    }
    if (!inspected->plan
        || !inspected->compile_request
        || !inspected->compiles
        || !inspected->link_request
        || !inspected->link) {
        diagnostics::print_error(
            "module target inspection reported a ready graph without complete compile/link state");
        return 4;
    }

    document.module_graph->compile_levels.reserve(
        inspected->plan->compile_levels.size());
    for (const auto& level : inspected->plan->compile_levels) {
        auto& rendered = document.module_graph->compile_levels.emplace_back();
        rendered.reserve(level.size());
        for (const auto& source : level) {
            rendered.push_back(plan::display_path(
                context.project.project_root,
                source));
        }
    }

    for (std::size_t level_index = 0;
         level_index < inspected->plan->compile_levels.size();
         ++level_index) {
        for (const auto& source : inspected->plan->compile_levels[level_index]) {
            if (const auto* compile = find_compile_inspection(
                    *inspected->compiles,
                    source)) {
                const bool planned = !compile->result.plan.empty();
                plan::Step step{
                    .kind = "compile",
                    .label = plan::display_path(
                        context.project.project_root,
                        compile->source),
                    .planned = planned,
                    .reasons = plan::reason_texts(
                        compile->result.validation.reasons),
                    .owner = owner_for_source(context, compile->source),
                    .role = role_for_unit(compile->request.unit),
                    .level = level_index,
                };
                plan::append_outputs(step, compile->request.unit);
                if (planned) {
                    step.process = model_compile_process(
                        compile->request,
                        compile_executor);
                    if (!step.process) return 4;
                }
                document.steps.push_back(std::move(step));
                continue;
            }

            if (const auto* header = find_header_unit_inspection(
                    *inspected->compiles,
                    source)) {
                const bool planned = !header->result.plan.empty();
                plan::Step step{
                    .kind = "compile",
                    .label = plan::display_path(
                        context.project.project_root,
                        header->source),
                    .planned = planned,
                    .reasons = plan::reason_texts(
                        header->result.validation.reasons),
                    .owner = "project",
                    .role = "header_unit",
                    .level = level_index,
                };
                plan::append_outputs(step, header->request.unit);
                if (planned) {
                    step.process = model_compile_process(
                        header->request,
                        compile_executor);
                    if (!step.process) return 4;
                }
                document.steps.push_back(std::move(step));
                continue;
            }

            diagnostics::print_error(
                "module dependency graph contains a compile node absent from compile-wave inspection: "
                + diagnostics::path_text(source));
            return 4;
        }
    }

    const bool link_planned = !inspected->link->plan.empty();
    plan::Step link_step{
        .kind = "link",
        .label = plan::display_path(
            context.project.project_root,
            inspected->link_request->output),
        .planned = link_planned,
        .reasons = plan::reason_texts(inspected->link->validation.reasons),
        .outputs = {inspected->link_request->output},
    };
    if (link_planned) {
        auto process = plan::model_link_process(
            context.toolchain,
            *inspected->link_request,
            *inspected->link);
        if (!process) {
            diagnostics::print_error(process.error());
            return 5;
        }
        link_step.process = std::move(*process);
    }
    document.steps.push_back(std::move(link_step));

    plan::render(format, context, document);
    return 0;
}

} // namespace mqb::app
