#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/TranslationUnit.hpp"
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
        expect(contains(*result, "/Z7"),
               "debug compilation should keep debug information inside the object for parallel safety");
        expect(!contains(*result, "/Zi"),
               "debug compilation must not emit a shared compiler PDB recipe");
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
        expect(!contains(*result, "/interface") && !contains(*result, "/ifcOutput"),
               "ordinary source compilation should not emit module-interface switches");

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

    auto cpp14 = invocation;
    cpp14.options.standard = mqb::CppStandard::cpp14;
    cpp14.options.additional_arguments.clear();
    const auto cpp14_result = mqb::msvc::MsvcCompiler::build_arguments(cpp14);
    expect(cpp14_result.has_value(), "ordinary C++14 compile invocation should be supported");
    if (cpp14_result) {
        expect(contains(*cpp14_result, "/std:c++14"), "C++14 should map correctly");
    }

    auto cpp17 = invocation;
    cpp17.options.standard = mqb::CppStandard::cpp17;
    cpp17.options.additional_arguments.clear();
    const auto cpp17_result = mqb::msvc::MsvcCompiler::build_arguments(cpp17);
    expect(cpp17_result.has_value(), "ordinary C++17 compile invocation should be supported");
    if (cpp17_result) {
        expect(contains(*cpp17_result, "/std:c++17"), "C++17 should map correctly");
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
        expect(contains(*release_result, "/Z7"),
               "release compilation should also avoid a shared compiler PDB");
        expect(!contains(*release_result, "/Zi"),
               "release compilation must not emit a shared compiler PDB recipe");
        expect(contains(*release_result, "/DNDEBUG"), "release configuration should define NDEBUG");
        expect(contains(*release_result, "/std:c++latest"), "latest standard should map correctly");
        expect(!contains(*release_result, "/sourceDependencies"),
               "dependency switch should be omitted when no output path was requested");
    }

    {
        auto module = invocation;
        module.source = "modules/math.ixx";
        module.object = "build/modules/math.ixx.obj";
        module.source_dependencies = "deps/modules/math.ixx.json";
        module.kind = mqb::TranslationUnitKind::module_interface;
        module.module_interface_output = "ifc/modules/math.ixx.ifc";
        module.module_references = {
            mqb::msvc::ModuleReference{
                .logical_name = "base",
                .interface_file = "ifc/base.ixx.ifc",
            },
        };
        module.options.additional_arguments = {
            "/ifcOutput",
            "ignored.ifc",
            "/Foignored-module.obj",
        };

        const auto module_result = mqb::msvc::MsvcCompiler::build_arguments(module);
        expect(module_result.has_value(), "module interface invocation should produce argv");
        if (module_result) {
            expect(contains(*module_result, "/interface") && contains(*module_result, "/TP"),
                   "module interface should use explicit /interface and /TP mode");
            expect(contains(*module_result, "/reference"),
                   "module imports should use MSVC /reference mapping");
            expect(contains(*module_result, "base=ifc/base.ixx.ifc"),
                   "module reference should preserve logical-name to IFC mapping");
            expect(contains(*module_result, "/ifcOutput"),
                   "module interface should route its IFC explicitly");
            expect(contains(*module_result, "ifc/modules/math.ixx.ifc"),
                   "structured IFC path should be present");

            const auto raw_ifc = std::find(module_result->begin(), module_result->end(), "ignored.ifc");
            const auto planned_ifc = std::find(
                module_result->begin(), module_result->end(), "ifc/modules/math.ixx.ifc");
            const auto raw_fo = std::find(
                module_result->begin(), module_result->end(), "/Foignored-module.obj");
            const auto planned_fo = std::find(
                module_result->begin(), module_result->end(), "/Fobuild/modules/math.ixx.obj");
            expect(raw_ifc != module_result->end()
                       && planned_ifc != module_result->end()
                       && raw_ifc < planned_ifc,
                   "structured IFC routing should follow raw /ifcOutput arguments");
            expect(raw_fo != module_result->end()
                       && planned_fo != module_result->end()
                       && raw_fo < planned_fo,
                   "structured module object routing should follow raw /Fo arguments");
            expect(module_result->back() == "modules/math.ixx",
                   "module source should remain the final argv element");
        }
    }

    {
        auto pre20_module = invocation;
        pre20_module.kind = mqb::TranslationUnitKind::module_interface;
        pre20_module.module_interface_output = "ifc/legacy.ifc";
        pre20_module.options.standard = mqb::CppStandard::cpp17;
        pre20_module.options.additional_arguments.clear();
        auto rejected = mqb::msvc::MsvcCompiler::build_arguments(pre20_module);
        expect(!rejected
                   && rejected.error().code == mqb::msvc::CompilerErrorCode::invalid_request,
               "module interface should fail closed below C++20");
        if (!rejected) {
            expect(rejected.error().message.find("C++20") != std::string::npos,
                   "pre-C++20 module rejection should explain the minimum standard");
        }
    }

    {
        mqb::msvc::HeaderUnitCompileInvocation header;
        header.header_name = "legacy.hpp";
        header.lookup_method = mqb::msvc::HeaderUnitLookupMethod::quote;
        header.interface_output = "ifc/legacy.hpp.ifc";
        header.options.standard = mqb::CppStandard::cpp14;
        auto rejected = mqb::msvc::MsvcCompiler::build_header_unit_arguments(header);
        expect(!rejected
                   && rejected.error().code == mqb::msvc::CompilerErrorCode::invalid_request,
               "header-unit producer should fail closed below C++20");
        if (!rejected) {
            expect(rejected.error().message.find("C++20") != std::string::npos,
                   "pre-C++20 header-unit rejection should explain the minimum standard");
        }
    }

    {
        auto consumer = invocation;
        consumer.module_references = {
            mqb::msvc::ModuleReference{
                .logical_name = "math",
                .interface_file = "ifc/math.ixx.ifc",
            },
        };
        consumer.options.additional_arguments.clear();
        auto consumer_result = mqb::msvc::MsvcCompiler::build_arguments(consumer);
        expect(consumer_result.has_value(), "ordinary source may consume named module IFC references");
        if (consumer_result) {
            expect(!contains(*consumer_result, "/interface") && !contains(*consumer_result, "/ifcOutput"),
                   "module-consuming ordinary source must remain an ordinary compile");
            expect(contains(*consumer_result, "math=ifc/math.ixx.ifc"),
                   "ordinary consumer should map imported module name to IFC path");
        }
    }

    auto invalid = invocation;
    invalid.source.clear();
    const auto invalid_result = mqb::msvc::MsvcCompiler::build_arguments(invalid);
    expect(!invalid_result.has_value(), "empty source path should fail validation");
    if (!invalid_result) {
        expect(invalid_result.error().code == mqb::msvc::CompilerErrorCode::invalid_request,
               "invalid invocation should report invalid_request");
    }

    {
        auto missing_ifc = invocation;
        missing_ifc.kind = mqb::TranslationUnitKind::module_interface;
        auto invalid_module = mqb::msvc::MsvcCompiler::build_arguments(missing_ifc);
        expect(!invalid_module
                   && invalid_module.error().code == mqb::msvc::CompilerErrorCode::invalid_request,
               "module interface without IFC output should fail before launching cl.exe");
    }

    {
        auto ordinary_ifc = invocation;
        ordinary_ifc.module_interface_output = "unexpected.ifc";
        auto invalid_ordinary = mqb::msvc::MsvcCompiler::build_arguments(ordinary_ifc);
        expect(!invalid_ordinary
                   && invalid_ordinary.error().code == mqb::msvc::CompilerErrorCode::invalid_request,
               "ordinary source must not request an IFC output");
    }

    {
        auto duplicate_reference = invocation;
        duplicate_reference.module_references = {
            mqb::msvc::ModuleReference{.logical_name = "M", .interface_file = "one.ifc"},
            mqb::msvc::ModuleReference{.logical_name = "M", .interface_file = "two.ifc"},
        };
        auto invalid_reference = mqb::msvc::MsvcCompiler::build_arguments(duplicate_reference);
        expect(!invalid_reference
                   && invalid_reference.error().code == mqb::msvc::CompilerErrorCode::invalid_request,
               "duplicate logical module references should fail closed");
    }

    {
        auto empty_reference = invocation;
        empty_reference.module_references = {
            mqb::msvc::ModuleReference{.logical_name = "", .interface_file = "one.ifc"},
        };
        auto invalid_reference = mqb::msvc::MsvcCompiler::build_arguments(empty_reference);
        expect(!invalid_reference
                   && invalid_reference.error().code == mqb::msvc::CompilerErrorCode::invalid_request,
               "empty logical module reference should fail validation");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_msvc_compiler_arguments_tests passed\n";
    return 0;
}
