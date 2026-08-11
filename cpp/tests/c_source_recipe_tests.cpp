#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/msvc/MsvcCompiler.hpp"
#include "mqb/msvc/MsvcModuleDependencyScanner.hpp"

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
    mqb::msvc::CompileInvocation c;
    c.source = "src/helper.c";
    c.object = "build/helper.obj";
    c.source_dependencies = "deps/helper.json";
    c.options.configuration = mqb::BuildConfiguration::debug;
    c.options.architecture = mqb::Architecture::x64;
    c.options.standard = mqb::CppStandard::latest;
    c.options.defines = {"C_POLICY=1"};
    c.options.include_directories = {"include dir"};
    c.options.additional_arguments = {"/TP", "/W4"};

    auto arguments = mqb::msvc::MsvcCompiler::build_arguments(c);
    expect(arguments.has_value(), "ordinary .c source should produce an MSVC argv recipe");
    if (arguments) {
        expect(contains(*arguments, "/TC"), "C source should be forced through MSVC C mode");
        expect(!contains(*arguments, "/std:c++latest"),
               "C source must not inherit the target C++ language-standard switch");
        expect(!contains(*arguments, "/EHsc"),
               "C source must not receive C++ exception semantics");
        expect(!contains(*arguments, "/permissive-"),
               "C source must not receive the C++ conformance switch");
        expect(!contains(*arguments, "/Zc:__cplusplus"),
               "C source must not receive C++ __cplusplus policy");
        expect(!contains(*arguments, "/Zc:preprocessor"),
               "C source keeps the MSVC C preprocessor baseline rather than the C++ recipe");
        expect(contains(*arguments, "/sourceDependencies"),
               "C source should keep normal incremental dependency metadata");
        expect(contains(*arguments, "/DC_POLICY=1"),
               "C source should still receive project defines");
        expect(contains(*arguments, "/Iinclude dir"),
               "C source should still receive project include paths");
        const auto raw_tp = std::find(arguments->begin(), arguments->end(), "/TP");
        const auto structural_tc = std::find(arguments->begin(), arguments->end(), "/TC");
        expect(raw_tp != arguments->end()
                   && structural_tc != arguments->end()
                   && raw_tp < structural_tc,
               "structured /TC must follow raw /TP so .c language ownership cannot be overridden silently");
    }

    {
        auto invalid = c;
        invalid.module_references = {
            mqb::msvc::ModuleReference{.logical_name = "M", .interface_file = "M.ifc"},
        };
        auto rejected = mqb::msvc::MsvcCompiler::build_arguments(invalid);
        expect(!rejected
                   && rejected.error().code == mqb::msvc::CompilerErrorCode::invalid_request,
               "C source must fail closed if given a C++ module consumer contract");
        if (!rejected) {
            expect(rejected.error().message.find("C translation units") != std::string::npos,
                   "C/module rejection should explain the language boundary");
        }
    }

    {
        mqb::msvc::ModuleScanInvocation scan;
        scan.source = "src/helper.c";
        scan.output_file = "scan/helper.json";
        scan.options.standard = mqb::CppStandard::cpp23;
        auto rejected = mqb::msvc::MsvcModuleDependencyScanner::build_arguments(scan);
        expect(!rejected
                   && rejected.error().code == mqb::msvc::ModuleScanErrorCode::invalid_request,
               "C source must fail closed before C++ P1689 scanning");
        if (!rejected) {
            expect(rejected.error().message.find("C translation units") != std::string::npos,
                   "C/P1689 rejection should explain the language boundary");
        }
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_c_source_recipe_tests passed\n";
    return 0;
}
