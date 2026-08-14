#pragma once

#include <string>
#include <string_view>

#include "mqb/msvc/MsvcParameterEngine.hpp"

namespace mqb::msvc::detail {

[[nodiscard]] std::string upper_ascii(std::string_view value);
[[nodiscard]] std::string_view option_body(std::string_view argument) noexcept;

[[nodiscard]] ParameterClassification classify_compiler_parameter(std::string_view argument);
[[nodiscard]] ParameterClassification classify_linker_parameter(std::string_view argument);
[[nodiscard]] ParameterClassification classify_librarian_parameter(std::string_view argument);

[[nodiscard]] bool is_unregistered(const ParameterClassification& classification) noexcept;

} // namespace mqb::msvc::detail
