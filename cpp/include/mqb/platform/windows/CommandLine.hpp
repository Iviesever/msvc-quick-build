#pragma once

#include <span>
#include <string>
#include <string_view>

namespace mqb::platform::windows {

// Encodes one argv element using the Microsoft C/C++ command-line parsing
// rules. This is a platform boundary helper, not the process model itself.
[[nodiscard]] std::wstring quote_command_line_argument(std::wstring_view argument);

// CreateProcessW accepts one mutable command-line string even though MQB keeps
// executable and argv as separate structured values internally.
[[nodiscard]] std::wstring build_command_line(
    std::wstring_view executable,
    std::span<const std::wstring> arguments);

} // namespace mqb::platform::windows
