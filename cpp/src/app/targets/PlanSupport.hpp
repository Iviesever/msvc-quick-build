#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <utility>
#include <variant>

#include "mqb/core/BuildAction.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcLinker.hpp"
#include "mqb/msvc/MsvcParameterEngine.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"

namespace mqb::app::plan {

[[nodiscard]] inline mqb::msvc::CompileExecutionRequest
execution_request_for(
    const mqb::orchestration::IncrementalCompileRequest& request) {
    return mqb::msvc::CompileExecutionRequest{
        .unit = request.unit,
        .options = request.options,
        .source_dependencies_file = request.source_dependencies_file,
        .working_directory = request.working_directory,
    };
}

[[nodiscard]] inline std::expected<mqb::process::ProcessSpec, std::string>
model_link_process(
    const mqb::msvc::MsvcToolchain& toolchain,
    const mqb::orchestration::IncrementalLinkRequest& request,
    const mqb::orchestration::IncrementalLinkInspection& inspection) {
    if (inspection.plan.actions.size() != 1) {
        return std::unexpected(
            "mqb plan expected exactly one link action");
    }

    const auto* action = std::get_if<mqb::LinkAction>(
        &inspection.plan.actions.front());
    if (action == nullptr) {
        return std::unexpected(
            "mqb plan received a non-link action from link inspection");
    }

    const std::filesystem::path working_directory =
        request.working_directory.value_or(std::filesystem::path{});
    auto linker_file_routing = mqb::msvc::MsvcParameterEngine::linker_file_inputs(
        request.options.additional_arguments,
        working_directory);
    if (!linker_file_routing) {
        return std::unexpected(linker_file_routing.error().message);
    }

    const mqb::msvc::LinkInvocation invocation{
        .objects = action->objects,
        .output = action->output,
        .libraries = action->libraries,
        .options = request.options,
        .working_directory = working_directory,
        .force_full_link = inspection.validation.library_inputs_changed
            || inspection.validation.file_inputs_changed
            || linker_file_routing->requires_full_link
            || request.options.address_sanitizer_runtime_library.has_value(),
        .observe_library_search = true,
    };
    auto recipe = mqb::msvc::MsvcLinker::build_recipe(toolchain, invocation);
    if (!recipe) {
        return std::unexpected(
            "failed to model link recipe: " + recipe.error().message);
    }
    return std::move(recipe->process);
}

} // namespace mqb::app::plan
