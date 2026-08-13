#include "mqb/msvc/MsvcCompiler.hpp"

#include <expected>
#include <utility>

#include "CompilerArgumentBuilder.hpp"
#include "CompilerExecution.hpp"

namespace mqb::msvc {

std::expected<std::vector<std::string>, CompilerError>
MsvcCompiler::build_arguments(const CompileInvocation& invocation) {
    return detail::build_compile_arguments(invocation);
}

std::expected<std::vector<std::string>, CompilerError>
MsvcCompiler::build_header_unit_arguments(const HeaderUnitCompileInvocation& invocation) {
    return detail::build_header_unit_arguments(invocation);
}

std::expected<process::ProcessResult, CompilerError>
MsvcCompiler::compile(const CompileInvocation& invocation) const {
    auto arguments = build_arguments(invocation);
    if (!arguments) return std::unexpected(arguments.error());
    return detail::execute_compile(
        toolchain_, runner_, invocation, std::move(*arguments));
}

std::expected<process::ProcessResult, CompilerError>
MsvcCompiler::compile_header_unit(const HeaderUnitCompileInvocation& invocation) const {
    auto arguments = build_header_unit_arguments(invocation);
    if (!arguments) return std::unexpected(arguments.error());
    return detail::execute_header_unit_compile(
        toolchain_, runner_, invocation, std::move(*arguments));
}

} // namespace mqb::msvc
