#include "mqb/platform/windows/CommandLine.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace mqb::platform::windows {
namespace {

[[nodiscard]] bool needs_quotes(const std::wstring_view argument) noexcept {
    if (argument.empty()) {
        return true;
    }

    for (const wchar_t ch : argument) {
        if (ch == L' ' || ch == L'\t' || ch == L'\n' || ch == L'\v' || ch == L'"') {
            return true;
        }
    }
    return false;
}

[[nodiscard]] CommandLineEncodingError encoding_error(
    const DWORD native_code,
    std::string message) {
    return CommandLineEncodingError{
        .native_code = static_cast<std::uint32_t>(native_code),
        .message = std::move(message),
    };
}

} // namespace

std::expected<std::string, CommandLineEncodingError>
utf16_to_utf8(const std::wstring_view value) {
    if (value.find(L'\0') != std::wstring_view::npos) {
        return std::unexpected(encoding_error(
            ERROR_INVALID_PARAMETER,
            "Windows command-line argument contains an embedded NUL"));
    }
    if (value.empty()) {
        return std::string{};
    }
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::unexpected(encoding_error(
            ERROR_INSUFFICIENT_BUFFER,
            "Windows command-line argument is too large to encode as UTF-8"));
    }

    const int source_length = static_cast<int>(value.size());
    const int required = ::WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        source_length,
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required == 0) {
        return std::unexpected(encoding_error(
            ::GetLastError(),
            "Windows command-line argument is not valid UTF-16"));
    }

    std::string result(static_cast<std::size_t>(required), '\0');
    const int converted = ::WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        source_length,
        result.data(),
        required,
        nullptr,
        nullptr);
    if (converted != required) {
        return std::unexpected(encoding_error(
            ::GetLastError(),
            "failed to encode Windows command-line argument as UTF-8"));
    }
    return result;
}

std::wstring quote_command_line_argument(const std::wstring_view argument) {
    if (!needs_quotes(argument)) {
        return std::wstring{argument};
    }

    std::wstring result;
    result.reserve(argument.size() + 2);
    result.push_back(L'"');

    std::size_t backslash_count = 0;
    for (const wchar_t ch : argument) {
        if (ch == L'\\') {
            ++backslash_count;
            continue;
        }

        if (ch == L'"') {
            // Backslashes before a literal quote are doubled, then one extra
            // backslash escapes the quote itself.
            result.append(backslash_count * 2 + 1, L'\\');
            result.push_back(L'"');
            backslash_count = 0;
            continue;
        }

        result.append(backslash_count, L'\\');
        backslash_count = 0;
        result.push_back(ch);
    }

    // Backslashes immediately before the closing quote must be doubled or the
    // closing quote would be escaped by the child runtime's argv parser.
    result.append(backslash_count * 2, L'\\');
    result.push_back(L'"');
    return result;
}

std::wstring build_command_line(
    const std::wstring_view executable,
    const std::span<const std::wstring> arguments) {
    std::wstring result = quote_command_line_argument(executable);
    for (const auto& argument : arguments) {
        result.push_back(L' ');
        result += quote_command_line_argument(argument);
    }
    return result;
}

} // namespace mqb::platform::windows
