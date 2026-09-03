#include "mqb/msvc/MsvcCompiler.hpp"

#include <expected>
#include <utility>

#include "CompilerArgumentBuilder.hpp"
#include "CompilerExecution.hpp"
#include "mqb/platform/windows/PathIdentity.hpp"

namespace mqb::msvc {
namespace {

[[nodiscard]] bool same_toolchain_identity(
    const ToolchainIdentity& left,
    const ToolchainIdentity& right) {
    return mqb::platform::windows::path_identity_key(left.compiler)
            == mqb::platform::windows::path_identity_key(right.compiler)
        && left.version == right.version
        && left.binary_stamp == right.binary_stamp;
}

} // namespace

std::expected<std::vector<std::string>, CompilerError>
MsvcCompiler::build_arguments(const CompileInvocation& invocation) {
    return detail::build_compile_arguments(invocation);
}

std::expected<std::vector<std::string>, CompilerError>
MsvcCompiler::build_header_unit_arguments(const HeaderUnitCompileInvocation& invocation) {
    return detail::build_header_unit_arguments(invocation);
}

std::expected<MsvcCompileRecipe, CompilerError>
MsvcCompiler::build_recipe(
    const MsvcToolchain& toolchain,
    const CompileInvocation& invocation) {
    auto arguments = build_arguments(invocation);
    if (!arguments) return std::unexpected(arguments.error());

    return MsvcCompileRecipe{
        .toolchain = toolchain.identity,
        .invocation = invocation,
        .process = detail::make_compiler_process_spec(
            toolchain,
            std::move(*arguments),
            invocation.working_directory),
    };
}

std::expected<MsvcCompileRecipe, CompilerError>
MsvcCompiler::build_header_unit_recipe(
    const MsvcToolchain& toolchain,
    const HeaderUnitCompileInvocation& invocation) {
    auto arguments = build_header_unit_arguments(invocation);
    if (!arguments) return std::unexpected(arguments.error());

    return MsvcCompileRecipe{
        .toolchain = toolchain.identity,
        .invocation = invocation,
        .process = detail::make_compiler_process_spec(
            toolchain,
            std::move(*arguments),
            invocation.working_directory),
    };
}

std::expected<process::ProcessResult, CompilerError>
MsvcCompiler::execute_recipe(const MsvcCompileRecipe& recipe) const {
    if (!same_toolchain_identity(recipe.toolchain, toolchain_.identity)) {
        return std::unexpected(CompilerError{
            .code = CompilerErrorCode::invalid_request,
            .message = "MSVC compile recipe was built for a different compiler toolchain",
        });
    }
    return detail::execute_compile_recipe(runner_, recipe);
}

std::expected<process::ProcessResult, CompilerError>
MsvcCompiler::compile(const CompileInvocation& invocation) const {
    auto recipe = build_recipe(toolchain_, invocation);
    if (!recipe) return std::unexpected(recipe.error());
    return execute_recipe(*recipe);
}

std::expected<process::ProcessResult, CompilerError>
MsvcCompiler::compile_header_unit(const HeaderUnitCompileInvocation& invocation) const {
    auto recipe = build_header_unit_recipe(toolchain_, invocation);
    if (!recipe) return std::unexpected(recipe.error());
    return execute_recipe(*recipe);
}

} // namespace mqb::msvc
