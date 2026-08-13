#pragma once

#include <expected>

#include "mqb/msvc/MsvcCompiler.hpp"

namespace mqb::msvc::detail {

[[nodiscard]] std::expected<void, CompilerError>
validate_compile_invocation(const CompileInvocation& invocation);

[[nodiscard]] std::expected<void, CompilerError>
validate_header_unit_compile_invocation(const HeaderUnitCompileInvocation& invocation);

} // namespace mqb::msvc::detail
