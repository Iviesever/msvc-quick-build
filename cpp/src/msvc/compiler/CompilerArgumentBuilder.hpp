#pragma once

#include <expected>
#include <string>
#include <vector>

#include "mqb/msvc/MsvcCompiler.hpp"

namespace mqb::msvc::detail {

[[nodiscard]] std::expected<std::vector<std::string>, CompilerError>
build_compile_arguments(const CompileInvocation& invocation);

[[nodiscard]] std::expected<std::vector<std::string>, CompilerError>
build_header_unit_arguments(const HeaderUnitCompileInvocation& invocation);

} // namespace mqb::msvc::detail
