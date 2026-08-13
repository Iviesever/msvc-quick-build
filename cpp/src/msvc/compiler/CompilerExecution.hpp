#pragma once

#include <expected>
#include <string>
#include <vector>

#include "mqb/msvc/MsvcCompiler.hpp"

namespace mqb::msvc::detail {

[[nodiscard]] std::expected<process::ProcessResult, CompilerError>
execute_compile(
    const MsvcToolchain& toolchain,
    process::ProcessRunner& runner,
    const CompileInvocation& invocation,
    std::vector<std::string> arguments);

[[nodiscard]] std::expected<process::ProcessResult, CompilerError>
execute_header_unit_compile(
    const MsvcToolchain& toolchain,
    process::ProcessRunner& runner,
    const HeaderUnitCompileInvocation& invocation,
    std::vector<std::string> arguments);

} // namespace mqb::msvc::detail
