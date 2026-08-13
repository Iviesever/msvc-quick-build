#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include "mqb/msvc/MsvcSourceDependenciesReader.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path) {
    const auto bytes = path.generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()};
}

} // namespace

int main() {
    constexpr std::string_view valid = R"json(
{
  "Version": "1.2",
  "Data": {
    "Source": "C:\\project\\\u6d4b\u8bd5.cpp",
    "ProvidedModule": "",
    "ImportedModules": [],
    "Meta": {"nested": [1, true, null, {"ignored": "value"}]},
    "Includes": [
      "C:\\include\\a.hpp",
      "C:\\include\\quote-\"file.hpp",
      "C:\\include\\emoji-\ud83d\ude80.hpp",
      "C:\\include\\a.hpp"
    ]
  }
}
)json";

    const auto parsed = mqb::msvc::MsvcSourceDependenciesReader::parse(valid);
    expect(parsed.has_value(), "valid MSVC sourceDependencies JSON should parse");
    if (parsed) {
        expect(path_to_utf8(parsed->source).find("\xe6\xb5\x8b\xe8\xaf\x95.cpp") != std::string::npos,
               "unicode escapes should decode into UTF-8 paths");
        expect(parsed->includes.size() == 4,
               "reader should preserve Includes entries before all_files deduplication");
        expect(path_to_utf8(parsed->includes[1]).find("quote-\"file.hpp") != std::string::npos,
               "escaped quotes should decode correctly");
        expect(path_to_utf8(parsed->includes[2]).find("emoji-\xf0\x9f\x9a\x80.hpp") != std::string::npos,
               "surrogate pairs should decode to one Unicode code point");

        const auto all_files = parsed->all_files();
        expect(all_files.size() == 4,
               "all_files should contain Source plus unique include paths");
        expect(all_files.front() == parsed->source,
               "all_files should keep Source as the first dependency");
    }

    const std::string with_bom = "\xef\xbb\xbf" + std::string{valid};
    expect(mqb::msvc::MsvcSourceDependenciesReader::parse(with_bom).has_value(),
           "UTF-8 BOM should be accepted");

    const auto missing_data = mqb::msvc::MsvcSourceDependenciesReader::parse(
        R"json({"Version":"1.2"})json");
    expect(!missing_data.has_value(), "missing Data object should fail");
    if (!missing_data) {
        expect(missing_data.error().code == mqb::msvc::SourceDependenciesErrorCode::missing_data,
               "missing Data should have a precise error code");
    }

    const auto missing_source = mqb::msvc::MsvcSourceDependenciesReader::parse(
        R"json({"Data":{"Includes":[]}})json");
    expect(!missing_source.has_value(), "missing Data.Source should fail");
    if (!missing_source) {
        expect(missing_source.error().code == mqb::msvc::SourceDependenciesErrorCode::missing_source,
               "missing Source should have a precise error code");
    }

    const auto wrong_includes = mqb::msvc::MsvcSourceDependenciesReader::parse(
        R"json({"Data":{"Source":"main.cpp","Includes":"not-an-array"}})json");
    expect(!wrong_includes.has_value(), "Includes with the wrong JSON type should fail");
    if (!wrong_includes) {
        expect(wrong_includes.error().code == mqb::msvc::SourceDependenciesErrorCode::invalid_schema,
               "wrong Includes type should report invalid_schema");
    }

    const auto bad_surrogate = mqb::msvc::MsvcSourceDependenciesReader::parse(
        R"json({"Data":{"Source":"bad-\ud800.cpp","Includes":[]}})json");
    expect(!bad_surrogate.has_value(), "unpaired surrogate should fail strict string parsing");

    const auto trailing_data = mqb::msvc::MsvcSourceDependenciesReader::parse(
        R"json({"Data":{"Source":"main.cpp","Includes":[]}} trailing)json");
    expect(!trailing_data.has_value(), "trailing bytes after JSON root should fail");
    if (!trailing_data) {
        expect(trailing_data.error().code == mqb::msvc::SourceDependenciesErrorCode::invalid_json,
               "trailing bytes should report invalid_json");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_msvc_source_dependencies_tests passed\n";
    return 0;
}
