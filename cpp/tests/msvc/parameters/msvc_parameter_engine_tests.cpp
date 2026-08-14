#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/msvc/MsvcParameterEngine.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

} // namespace

int main() {
    using mqb::msvc::MsvcParameterEngine;
    using mqb::msvc::ParameterErrorCode;
    using mqb::msvc::ParameterOwnership;
    using mqb::msvc::ParameterTool;

    expect(
        MsvcParameterEngine::classify(ParameterTool::compiler, "/W4").ownership
            == ParameterOwnership::passthrough,
        "/W4 should be safe compiler passthrough");
    expect(
        MsvcParameterEngine::classify(ParameterTool::compiler, "/std:c++20").ownership
            == ParameterOwnership::semantic,
        "/std should be semantic compiler policy");
    expect(
        MsvcParameterEngine::classify(ParameterTool::compiler, "/Foowned.obj").ownership
            == ParameterOwnership::mqb_owned,
        "/Fo should remain MQB-owned structural routing");
    expect(
        MsvcParameterEngine::classify(ParameterTool::compiler, "/MP8").ownership
            == ParameterOwnership::unsupported,
        "/MP should be rejected because MQB owns TU scheduling");
    expect(
        MsvcParameterEngine::classify(ParameterTool::compiler, "/cgthreads8").ownership
            == ParameterOwnership::passthrough,
        "compiler prefix routing must not confuse /cgthreads with /c");
    expect(
        MsvcParameterEngine::classify(ParameterTool::compiler, "/EHsc").ownership
            == ParameterOwnership::passthrough,
        "compiler prefix routing must not confuse /EHsc with preprocess-only /E");
    expect(
        MsvcParameterEngine::classify(ParameterTool::compiler, "/DefinitelyNotMsvc").ownership
            == ParameterOwnership::unsupported,
        "unknown compiler options must not enter an unclassified passthrough path");

    {
        const std::vector<std::string> arguments{
            "/W4", "/arch:AVX2", "/std:c++20", "/MT", "/GL"};
        auto routed = MsvcParameterEngine::route_compiler(arguments);
        expect(routed.has_value(), "valid compiler routing should succeed");
        if (routed) {
            expect(routed->passthrough.size() == 2
                       && routed->passthrough[0] == "/W4"
                       && routed->passthrough[1] == "/arch:AVX2",
                   "safe compiler options should preserve exact order and spelling");
            expect(routed->standard == mqb::CppStandard::cpp20,
                   "C++ /std should normalize to typed standard");
            expect(routed->runtime_library == mqb::RuntimeLibrary::mt,
                   "CRT switch should normalize to typed runtime");
            expect(routed->link_time_code_generation.has_value()
                       && *routed->link_time_code_generation,
                   "/GL should normalize to coupled LTCG enablement");
        }
    }

    {
        const std::vector<std::string> arguments{"/std:c17", "/W3"};
        auto routed = MsvcParameterEngine::route_compiler(arguments);
        expect(routed.has_value(), "C language standard should remain valid raw compiler policy");
        if (routed) {
            expect(!routed->standard,
                   "C language /std mode must not populate CppStandard");
            expect(routed->passthrough.size() == 2
                       && routed->passthrough.front() == "/std:c17",
                   "C language mode should be preserved verbatim");
        }
    }

    {
        const std::vector<std::string> arguments{"/MD", "/MT"};
        auto routed = MsvcParameterEngine::route_compiler(arguments);
        expect(!routed
                   && routed.error().code == ParameterErrorCode::conflicting_semantic_option,
               "conflicting native CRT switches should fail before cl.exe");
    }

    {
        const std::vector<std::string> arguments{"/Foescape.obj"};
        auto routed = MsvcParameterEngine::route_compiler(arguments);
        expect(!routed && routed.error().code == ParameterErrorCode::owned_option,
               "MQB-owned compiler artifact routing should produce a dedicated error");
    }

    {
        const std::vector<std::string> arguments{"@hidden.rsp"};
        auto routed = MsvcParameterEngine::route_compiler(arguments);
        expect(!routed,
               "compiler response files should not bypass parameter ownership classification");
    }

    expect(
        MsvcParameterEngine::classify(ParameterTool::linker, "/stack:8388608").ownership
            == ParameterOwnership::passthrough,
        "linker registry should be case-insensitive");
    expect(
        MsvcParameterEngine::classify(ParameterTool::linker, "/OUT:escape.exe").ownership
            == ParameterOwnership::mqb_owned,
        "/OUT should remain MQB-owned linker routing");
    expect(
        MsvcParameterEngine::classify(ParameterTool::linker, "/DEBUG:FASTLINK").ownership
            == ParameterOwnership::unsupported,
        "removed /DEBUG:FASTLINK should be rejected explicitly");

    {
        const std::vector<std::string> arguments{
            "/STACK:8388608", "/DEBUG:FULL", "/machine:x86", "/subsystem:windows", "/ltcg"};
        auto routed = MsvcParameterEngine::route_linker(arguments);
        expect(routed.has_value(), "valid linker routing should succeed");
        if (routed) {
            expect(routed->passthrough.size() == 2
                       && routed->passthrough[0] == "/STACK:8388608"
                       && routed->passthrough[1] == "/DEBUG:FULL",
                   "safe linker passthrough should preserve exact argv");
            expect(routed->architecture == mqb::Architecture::x86,
                   "/MACHINE should normalize case-insensitively");
            expect(routed->subsystem == mqb::LinkSubsystem::windows,
                   "/SUBSYSTEM should normalize to typed policy");
            expect(routed->link_time_code_generation.has_value()
                       && *routed->link_time_code_generation,
                   "/LTCG should normalize to typed policy");
        }
    }

    {
        const std::vector<std::string> arguments{"/LTCG:INCREMENTAL"};
        auto routed = MsvcParameterEngine::route_linker(arguments);
        expect(!routed,
               "LTCG variants not representable by the current typed model should fail closed");
    }

    {
        const std::vector<std::string> arguments{"/MACHINE:X64", "/LTCG", "/WX"};
        auto routed = MsvcParameterEngine::route_librarian(arguments);
        expect(routed.has_value(), "valid librarian routing should succeed");
        if (routed) {
            expect(routed->architecture == mqb::Architecture::x64,
                   "librarian /MACHINE should normalize to typed architecture");
            expect(routed->link_time_code_generation.has_value()
                       && *routed->link_time_code_generation,
                   "librarian /LTCG should normalize to typed policy");
            expect(routed->passthrough.size() == 1 && routed->passthrough.front() == "/WX",
                   "safe librarian option should remain passthrough");
        }
    }

    expect(
        MsvcParameterEngine::classify(ParameterTool::librarian, "/OUT:escape.lib").ownership
            == ParameterOwnership::mqb_owned,
        "librarian /OUT should remain MQB-owned");
    expect(
        MsvcParameterEngine::classify(ParameterTool::librarian, "/EXTRACT:member.obj").ownership
            == ParameterOwnership::unsupported,
        "lib.exe mode-changing operations should be rejected by build routing");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_parameter_engine_tests passed\n";
    return 0;
}
