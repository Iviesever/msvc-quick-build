#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace mqb::platform::windows {

struct CommandLineEncodingError {
    std::uint32_t native_code{};
    std::string message;
};

// Convert one Windows UTF-16 argv element into MQB's internal UTF-8 text
// contract without depending on the active ANSI/OEM code page.
[[nodiscard]] std::expected<std::string, CommandLineEncodingError>
utf16_to_utf8(std::wstring_view value);

// Encodes one argv element using the Microsoft C/C++ command-line parsing
// rules. This is a platform boundary helper, not the process model itself.
[[nodiscard]] std::wstring quote_command_line_argument(std::wstring_view argument);

// CreateProcessW accepts one mutable command-line string even though MQB keeps
// executable and argv as separate structured values internally.
[[nodiscard]] std::wstring build_command_line(
    std::wstring_view executable,
    std::span<const std::wstring> arguments);

} // namespace mqb::platform::windows
