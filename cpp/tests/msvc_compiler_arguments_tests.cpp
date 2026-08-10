#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/core/CompilerOptions.hpp"
#include "mqb/msvc/MsvcCompiler.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

[[nodiscard]] bool contains(
    const std::vector<std::string>& arguments,
    const std::string_view expected) {
    return std::find(arguments.begin(), arguments.end(), expected) != arguments.end();
}

} // namespace

int main() {
    mqb::msvc::CompileInvocation invocation;
    invocation.source = std::filesystem::path{"src/my file.cpp"};
    invocation.object = std::filesystem::path{"build/my file.obj"};
    invocation.source_dependencies = std::filesystem::path{"deps/my file.json"};
    invocation.options.configuration = mqb::BuildConfiguration::debug;
    invocation.options.architecture = mqb::Architecture::x64;
    invocation.options.standard = mqb::CppStandard::cpp23;
    invocation.options.defines = {"UNICODE", "FEATURE=7"};
    invocation.options.include_directories = {
        std::filesystem::path{"include"},
        std::filesystem::path{"vendor include"},
    };
    invocation.options.additional_arguments = {"/volatile:iso", "/Foignored.obj"};

    const auto result = mqb::msvc::MsvcCompiler::build_arguments(invocation);
    expect(result.has_value(), "valid compile invocation should produce argv");
    if (result) {
        expect(contains(*result, "/nologo"), "argv should suppress the MSVC banner");
        expect(contains(*result, "/c"), "argv should request compile-only mode");
        expect(contains(*result, "/utf-8"), "argv should use UTF-8 source/execution encoding");
        expect(contains(*result, "/Od"), "debug configuration should disable optimization");
        expect(contains(*result, "/MDd"), "debug configuration should select debug DLL CRT");
        expect(contains(*result, "/D_DEBUG"), "debug configuration should define _DEBUG");
        expect(contains(*result, "/std:c++23preview"),
               "C++23 should map to MSVC's current C++23 preview switch");
        expect(!contains(*result, "/std:c++23"),
               "unsupported /std:c++23 must not be emitted before MSVC implements it");
        expect(contains(*result, "/DUNICODE"), "explicit defines should become /D argv elements");
        expect(contains(*result, "/DFEATURE=7"), "define values should be preserved");
        expect(contains(*result, "/Iinclude"), "include path should become one /I argv element");
        expect(contains(*result, "/Ivendor include"), "include path with spaces should remain one argv element");
        expect(contains(*result, "/sourceDependencies"), "dependency output should enable /sourceDependencies");

        expect(result->size() >= 2, "argv should end with object routing and source");
        if (result->size() >= 2) {
            expect((*result)[result->size() - 2] == "/Fobuild/my file.obj",
                   "structured object path should be emitted after raw flags");
            expect((*result).back() == "src/my file.cpp",
                   "source should be the final argv element");
        }

        const auto raw_fo = std::find(result->begin(), result->end(), "/Foignored.obj");
        const auto structured_fo = std::find(result->begin(), result->end(), "/Fobuild/my file.obj");
        expect(raw_fo != result->end() && structured_fo != result->end() && raw_fo < structured_fo,
               "structured object routing should win by appearing after a raw /Fo");
    }

    auto cpp20 = invocation;
    cpp20.options.standard = mqb::CppStandard::cpp20;
    cpp20.options.additional_arguments.clear();
    const auto cpp20_result = mqb::msvc::MsvcCompiler::build_arguments(cpp20);
    expect(cpp20_result.has_value(), "C++20 compile invocation should be supported");
    if (cpp20_result) {
        expect(contains(*cpp20_result, "/std:c++20"), "C++20 should map correctly");
    }

    auto release = invocation;
    release.source_dependencies.reset();
    release.options.configuration = mqb::BuildConfiguration::release;
    release.options.standard = mqb::CppStandard::latest;
    release.options.additional_arguments.clear();
    const auto release_result = mqb::msvc::MsvcCompiler::build_arguments(release);
    expect(release_result.has_value(), "release compile invocation should be supported");
    if (release_result) {
        expect(contains(*release_result, "/O2"), "release configuration should optimize");
        expect(contains(*release_result, "/MD"), "release configuration should select release DLL CRT");
        expect(contains(*release_result, "/DNDEBUG"), "release configuration should define NDEBUG");
        expect(contains(*release_result, "/std:c++latest"), "latest standard should map correctly");
        expect(!contains(*release_result, "/sourceDependencies"),
               "dependency switch should be omitted when no output path was requested");
    }

    auto invalid = invocation;
    invalid.source.clear();
    const auto invalid_result = mqb::msvc::MsvcCompiler::build_arguments(invalid);
    expect(!invalid_result.has_value(), "empty source path should fail validation");
    if (!invalid_result) {
        expect(invalid_result.error().code == mqb::msvc::CompilerErrorCode::invalid_request,
               "invalid invocation should report invalid_request");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_msvc_compiler_arguments_tests passed\n";
    return 0;
}
