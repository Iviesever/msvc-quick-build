#include "mqb/platform/windows/CommandLine.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

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

} // namespace

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
