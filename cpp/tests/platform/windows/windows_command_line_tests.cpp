#include <windows.h>
#include <shellapi.h>

#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/platform/windows/CommandLine.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void expect_round_trip(
    const std::wstring_view executable,
    const std::vector<std::wstring>& arguments,
    const std::string_view label) {
    const auto command_line = mqb::platform::windows::build_command_line(executable, arguments);

    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(command_line.c_str(), &argc);
    if (argv == nullptr) {
        ++failures;
        std::cerr << "FAIL: " << label << " (CommandLineToArgvW failed)\n";
        return;
    }

    const auto expected_count = static_cast<int>(arguments.size() + 1);
    if (argc != expected_count) {
        ++failures;
        std::cerr << "FAIL: " << label << " (argc mismatch)\n";
        ::LocalFree(argv);
        return;
    }

    if (std::wstring_view{argv[0]} != executable) {
        ++failures;
        std::cerr << "FAIL: " << label << " (executable mismatch)\n";
    }

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (std::wstring_view{argv[index + 1]} != arguments[index]) {
            ++failures;
            std::cerr << "FAIL: " << label << " (argument " << index << " mismatch)\n";
        }
    }

    ::LocalFree(argv);
}

} // namespace

int main() {
    using mqb::platform::windows::quote_command_line_argument;

    expect(quote_command_line_argument(L"plain") == L"plain",
           "plain argument should not be quoted unnecessarily");
    expect(quote_command_line_argument(L"") == L"\"\"",
           "empty argument must survive as an explicit argv element");
    expect(quote_command_line_argument(L"two words") == L"\"two words\"",
           "whitespace should force quoting");

    expect_round_trip(
        L"C:\\Program Files\\MQB\\mqb.exe",
        {
            L"simple",
            L"two words",
            L"",
            L"quote\"inside",
            L"C:\\path with spaces\\",
            L"ends-with-backslash\\",
            L"many\\\\slashes",
            L"many\\\\\"quoted",
            L"tab\tinside",
            L"unicode-日本語-测试",
        },
        "complex argv should round-trip through Windows parsing rules");

    expect_round_trip(
        L"mqb.exe",
        {L"\\\\server\\share\\folder with space\\", L"a\\\"b", L"\\\""},
        "UNC paths and quote-adjacent backslashes should round-trip");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_windows_command_line_tests passed\n";
    return 0;
}
