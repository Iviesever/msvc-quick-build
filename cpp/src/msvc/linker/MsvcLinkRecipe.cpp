#include "mqb/msvc/MsvcLinker.hpp"

#include <expected>
#include <utility>

namespace mqb::msvc {

std::expected<MsvcLinkRecipe, LinkerError>
MsvcLinker::build_recipe(
    const MsvcToolchain& toolchain,
    const LinkInvocation& invocation) {
    auto arguments = build_arguments(invocation);
    if (!arguments) {
        return std::unexpected(arguments.error());
    }

    process::ProcessSpec spec;
    spec.executable = toolchain.linker;
    spec.arguments = std::move(*arguments);
    spec.working_directory = invocation.working_directory;
    spec.environment = toolchain.environment;
    // link.exe prepends LINK and appends _LINK_ to explicit argv, and LINK_REPRO
    // can create unowned diagnostic state. The recipe owns the same isolation as
    // real MQB link execution.
    spec.environment.push_back(process::EnvironmentVariable{"LINK", {}, true});
    spec.environment.push_back(process::EnvironmentVariable{"_LINK_", {}, true});
    spec.environment.push_back(process::EnvironmentVariable{"LINK_REPRO", {}, true});
    spec.inherit_environment = true;
    spec.capture_stdout = true;
    spec.capture_stderr = true;

    return MsvcLinkRecipe{
        .invocation = invocation,
        .process = std::move(spec),
    };
}

std::expected<process::ProcessResult, LinkerError>
MsvcLinker::execute_recipe(const MsvcLinkRecipe& recipe) const {
    // Keep output preparation and process launch execution-only. Recipe
    // construction itself remains pure and can therefore be used by `mqb plan`.
    return link(recipe.invocation);
}

} // namespace mqb::msvc
