#include <windows.h>
#include <shellapi.h>

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/platform/windows/CommandLine.hpp"
#include "mqb/platform/windows/PathIdentity.hpp"

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

void expect_path_key_matches_windows_ordinal(
    const std::wstring_view left,
    const std::wstring_view right,
    const std::string_view label) {
    const int comparison = ::CompareStringOrdinal(
        left.data(),
        static_cast<int>(left.size()),
        right.data(),
        static_cast<int>(right.size()),
        TRUE);
    if (comparison == 0) {
        ++failures;
        std::cerr << "FAIL: " << label << " (CompareStringOrdinal failed)\n";
        return;
    }

    const bool windows_equal = comparison == CSTR_EQUAL;
    const bool key_equal =
        mqb::platform::windows::path_identity_key(std::filesystem::path{left})
        == mqb::platform::windows::path_identity_key(std::filesystem::path{right});
    if (windows_equal != key_equal) {
        ++failures;
        std::cerr << "FAIL: " << label
                  << " (path key equality disagrees with CompareStringOrdinal)\n";
    }
}

} // namespace

int main() {
    using mqb::platform::windows::path_identity_contains;
    using mqb::platform::windows::path_identity_key;
    using mqb::platform::windows::quote_command_line_argument;
    using mqb::platform::windows::utf16_to_utf8;

    expect(quote_command_line_argument(L"plain") == L"plain",
           "plain argument should not be quoted unnecessarily");
    expect(quote_command_line_argument(L"") == L"\"\"",
           "empty argument must survive as an explicit argv element");
    expect(quote_command_line_argument(L"two words") == L"\"two words\"",
           "whitespace should force quoting");

    const auto ascii_utf8 = utf16_to_utf8(L"plain");
    expect(ascii_utf8.has_value() && *ascii_utf8 == "plain",
           "UTF-16 ASCII argv should encode to identical UTF-8 text");
    const auto unicode_utf8 = utf16_to_utf8(L"路径-日本語-测试-🙂");
    expect(unicode_utf8.has_value() && *unicode_utf8 == "路径-日本語-测试-🙂",
           "UTF-16 Unicode argv should preserve exact code points in UTF-8");
    const auto empty_utf8 = utf16_to_utf8(L"");
    expect(empty_utf8.has_value() && empty_utf8->empty(),
           "empty UTF-16 argv should remain an explicit empty UTF-8 element");

    const std::wstring embedded_nul{L'a', L'\0', L'b'};
    expect(!utf16_to_utf8(embedded_nul).has_value(),
           "embedded NUL must fail closed at the Windows argv encoding boundary");
    const std::wstring unpaired_high_surrogate{
        static_cast<wchar_t>(0xD800)};
    expect(!utf16_to_utf8(unpaired_high_surrogate).has_value(),
           "invalid UTF-16 surrogate input must fail closed instead of replacement encoding");

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

    expect(
        path_identity_contains(
            L"C:\\Work\\MQB",
            L"c:\\work\\mqb\\src\\main.cpp"),
        "project containment should use Windows case-insensitive identity");
    expect(
        path_identity_contains(
            L"C:\\Work\\MQB\\",
            L"c:\\work\\mqb"),
        "project containment should treat trailing separators and root aliases as identical");
    expect(
        !path_identity_contains(
            L"C:\\Work\\MQB",
            L"C:\\Work\\MQB2\\main.cpp"),
        "project containment must enforce a component boundary");
    expect(
        path_identity_contains(
            L"C:\\项目\\MQB",
            L"c:\\项目\\mqb\\src\\main.cpp"),
        "project containment should preserve non-ASCII path components while folding ASCII case");
    expect(
        !path_identity_contains(
            L"C:\\项目\\MQB",
            L"C:\\項目\\MQB\\src\\main.cpp"),
        "project containment must not conflate distinct non-ASCII code points");

    expect(
        path_identity_key(L"C:\\Café\\Écho.cpp")
            == path_identity_key(L"c:\\CAFÉ\\éCHO.cpp"),
        "Windows Unicode case aliases must share one path identity key");
    expect(
        path_identity_contains(
            L"C:\\Café\\Éditeur",
            L"c:\\CAFÉ\\éditeur\\src\\main.cpp"),
        "project containment must honor non-ASCII Windows case aliases");
    expect(
        path_identity_key(L"C:\\Ångström")
            != path_identity_key(L"C:\\A\u030Angström"),
        "ordinal path identity must not normalize canonically equivalent Unicode sequences");

    // The canonical key is only valid if its equality relation is exactly the
    // Windows ordinal, case-insensitive relation used for NTFS-style names.
    // Include pairs where linguistic/full Unicode casing could differ from the
    // operating-system file-system uppercase table so an over-aggressive string
    // mapping cannot silently create false cache/path collisions.
    expect_path_key_matches_windows_ordinal(
        L"C:\\Café\\Écho.cpp",
        L"c:\\CAFÉ\\éCHO.cpp",
        "accented Latin case alias");
    expect_path_key_matches_windows_ordinal(
        L"C:\\Straße\\main.cpp",
        L"C:\\STRASSE\\main.cpp",
        "sharp-s versus SS");
    expect_path_key_matches_windows_ordinal(
        L"C:\\ﬃ\\main.cpp",
        L"C:\\FFI\\main.cpp",
        "ligature versus expanded letters");
    expect_path_key_matches_windows_ordinal(
        L"C:\\Σ\\main.cpp",
        L"C:\\σ\\main.cpp",
        "Greek sigma case alias");
    expect_path_key_matches_windows_ordinal(
        L"C:\\Σ\\main.cpp",
        L"C:\\ς\\main.cpp",
        "Greek final sigma case alias");
    expect_path_key_matches_windows_ordinal(
        L"C:\\Ångström",
        L"C:\\A\u030Angström",
        "precomposed versus decomposed Unicode");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_windows_command_line_tests passed\n";
    return 0;
}
