#include <array>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/msvc/MsvcParameterEngine.hpp"

namespace {

int failures = 0;
constexpr std::string_view unregistered_marker =
    "not present in MQB's current MSVC parameter registry";

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void expect_registered(
    const mqb::msvc::ParameterTool tool,
    const std::string_view argument) {
    const auto classified = mqb::msvc::MsvcParameterEngine::classify(tool, argument);
    if (classified.rationale.find(unregistered_marker) != std::string::npos) {
        ++failures;
        std::cerr << "FAIL: official " << mqb::msvc::to_string(tool)
                  << " option family is unregistered: " << argument << '\n';
    }
}

void verify_current_official_coverage() {
    using mqb::msvc::ParameterTool;

    static constexpr std::array compiler_options{
        "@args.rsp", "/?", "/AIclr", "/analyze", "/arch:AVX2", "/arm64EC",
        "/await", "/bigobj", "/Brepro", "/Bt+", "/C", "/c", "/cgthreads8",
        "/clr", "/constexpr:depth512", "/DVALUE=1", "/dynamicdeopt",
        "/diagnostics:caret", "/doc:comments.xml", "/E", "/EHa", "/EP",
        "/errorReport:none", "/execution-charset:utf-8", "/experimental:log",
        "/experimental:module", "/exportHeader", "/external:anglebrackets",
        "/F1048576", "/FA", "/Faout.asm", "/fastfail", "/favor:AMD64",
        "/FC", "/Fdvc.pdb", "/Feapp.exe", "/feature:arch_feature_arm64_lse",
        "/forceInterlockedFunctions", "/FIforced.hpp", "/Fiout.i", "/Fmout.map",
        "/Foout.obj", "/Fpout.pch", "/fp:fast", "/fpcvt:IA", "/FRbrowse.sbr",
        "/FS", "/fsanitize=address", "/fsanitize-coverage=inline-8bit-counters",
        "/Ftgenerated", "/FUassembly.dll", "/Fxmerged.cpp", "/GA", "/Gd", "/Ge",
        "/GF", "/GH", "/Gh", "/GL", "/Gm-", "/GR-", "/Gr", "/GS-", "/Gs4096",
        "/GT", "/Gu-", "/guard:cf", "/Gv", "/Gw-", "/GX", "/Gy-", "/GZ", "/Gz",
        "/H", "/headerName:quote", "/headerUnit:quote", "/HELP", "/homeparams",
        "/hotpatch", "/Iinclude", "/ifcOutput", "/interface", "/internalPartition",
        "/J", "/jumptablerdata", "/JMC", "/kernel", "/LD", "/LDd", "/link", "/LN",
        "/MD", "/MDd", "/MP8", "/MT", "/MTd", "/nologo", "/O1", "/O2", "/Ob2",
        "/Od", "/Og", "/Oi-", "/openmp", "/openmp:experimental", "/openmp:llvm",
        "/options:strict", "/Os", "/Ot", "/Ox", "/Oy", "/P", "/PD", "/permissive-",
        "/PH", "/presetPadding", "/Qfast_transcendentals", "/QIfist",
        "/Qimprecise_fwaits", "/QIntel-jcc-erratum", "/Qpar-report:2", "/Qpar",
        "/Qsafe_fp_loads", "/Qspectre", "/Qspectre-load", "/Qspectre-load-cf",
        "/Qvec-report:2", "/reference", "/RTC1", "/RTCc", "/RTCs", "/RTCu",
        "/scanDependencies", "/sdl", "/showIncludes", "/source-charset:utf-8",
        "/sourceDependencies", "/sourceDependencies:directives", "/std:c++14",
        "/std:c++17", "/std:c++20", "/std:c++latest", "/std:c11", "/std:c17",
        "/std:clatest", "/TC", "/Tcfile.c", "/TP", "/Tpfile.cpp", "/translateInclude",
        "/UNAME", "/u", "/utf-8", "/V", "/validate-charset", "/vd2", "/vlen",
        "/vmb", "/vmg", "/vmm", "/vms", "/vmv", "/volatile:iso", "/volatileMetadata",
        "/w", "/W0", "/W1", "/W2", "/W3", "/W4", "/w14242", "/Wall", "/wd4996",
        "/we4715", "/WL", "/wo4267", "/Wv:19.40", "/WX", "/X", "/Y-", "/Ycpch.hpp",
        "/Yd", "/Ylfoo", "/Yupch.hpp", "/Z7", "/Za", "/Zc:__cplusplus", "/Ze", "/Zf",
        "/Zg", "/ZH:SHA_256", "/ZI", "/Zi", "/Zl", "/Zm200", "/Zo", "/Zp8", "/Zs",
        "/ZW",
    };
    for (const auto option : compiler_options) expect_registered(ParameterTool::compiler, option);

    static constexpr std::array linker_options{
        "@args.rsp", "/ALIGN:4096", "/ALLOWBIND", "/ALLOWISOLATION", "/APPCONTAINER",
        "/ARM64XFUNCTIONPADMINX64:16", "/ASSEMBLYDEBUG", "/ASSEMBLYLINKRESOURCE:r.netmodule",
        "/ASSEMBLYMODULE:m.netmodule", "/ASSEMBLYRESOURCE:r.resources", "/BASE:0x140000000",
        "/CETCOMPAT", "/CGTHREADS:4", "/CLRIMAGETYPE:IJW", "/CLRSUPPORTLASTERROR",
        "/CLRTHREADATTRIBUTE:MTA", "/CLRUNMANAGEDCODECHECK", "/DEBUG", "/DEBUGTYPE:CV",
        "/DEF:exports.def", "/DEFAULTLIB:user32.lib", "/DELAY:UNLOAD", "/DELAYLOAD:foo.dll",
        "/DELAYSIGN", "/DEPENDENTLOADFLAG:0x800", "/DLL", "/DRIVER", "/DYNAMICBASE",
        "/DYNAMICDEOPT", "/ENTRY:mainCRTStartup", "/ERRORREPORT:NONE", "/EXPORT:foo",
        "/FILEALIGN:512", "/FIXED", "/FORCE", "/FUNCTIONPADMIN", "/GENPROFILE", "/GUARD:CF",
        "/HEAP:1048576", "/HIGHENTROPYVA", "/IDLOUT:out.idl", "/IGNORE:4099", "/IGNOREIDL",
        "/ILK:out.ilk", "/IMPLIB:out.lib", "/INCLUDE:symbol", "/INCREMENTAL",
        "/INFERASANLIBS", "/INTEGRITYCHECK", "/KERNEL", "/KEYCONTAINER:key",
        "/KEYFILE:key.snk", "/LARGEADDRESSAWARE", "/LIBPATH:lib", "/LINKREPRO:repro",
        "/LINKREPROFULLPATHRSP:full.rsp", "/LINKREPROTARGET:app.exe", "/LTCG", "/MACHINE:X64",
        "/MANIFEST", "/MANIFESTDEPENDENCY:type='win32'", "/MANIFESTFILE:app.manifest",
        "/MANIFESTINPUT:input.manifest", "/MANIFESTUAC:level='asInvoker'", "/MAP", "/MAPINFO:EXPORTS",
        "/MERGE:.rdata=.text", "/MIDL:@midl.rsp", "/NATVIS:view.natvis", "/NOASSEMBLY",
        "/NODEFAULTLIB", "/NOENTRY", "/NOFUNCTIONPADSECTION", "/NOLOGO", "/NXCOMPAT",
        "/OPT:REF", "/ORDER:@order.txt", "/OUT:app.exe", "/PDB:app.pdb", "/PDBALTPATH:%_PDB%",
        "/PDBSTRIPPED:public.pdb", "/PGD:app.pgd", "/POGOSAFEMODE", "/PROFILE", "/RELEASE",
        "/SAFESEH", "/SECTION:.text,ER", "/SOURCELINK:sourcelink.json", "/SPD:profile.spd",
        "/SPDEMBED:profile.spd", "/SPDIN:profile.spd", "/SPGO", "/STACK:8388608",
        "/STUB:stub.exe", "/SUBSYSTEM:CONSOLE", "/SWAPRUN:NET", "/TIME", "/TLBID:1",
        "/TLBOUT:app.tlb", "/TSAWARE", "/USEPROFILE", "/VERBOSE", "/VERSION:1.0",
        "/WHOLEARCHIVE:all.lib", "/WINMD", "/WINMDFILE:app.winmd", "/WINMDKEYFILE:key.snk",
        "/WINMDKEYCONTAINER:key", "/WINMDDELAYSIGN", "/WX",
    };
    for (const auto option : linker_options) expect_registered(ParameterTool::linker, option);

    static constexpr std::array librarian_options{
        "@args.rsp", "/DEF:exports.def", "/ERRORREPORT:NONE", "/EXPORT:foo",
        "/EXTRACT:member.obj", "/INCLUDE:symbol", "/LIBPATH:lib", "/LINKREPRO:repro",
        "/LINKREPROTARGET:out.lib", "/LIST", "/LTCG", "/MACHINE:X64", "/NAME:foo.dll",
        "/NODEFAULTLIB", "/NOLOGO", "/OUT:out.lib", "/REMOVE:member.obj",
        "/SUBSYSTEM:CONSOLE", "/VERBOSE", "/WX",
    };
    for (const auto option : librarian_options) expect_registered(ParameterTool::librarian, option);
}

} // namespace

int main() {
    using mqb::msvc::MsvcParameterEngine;
    using mqb::msvc::ParameterErrorCode;
    using mqb::msvc::ParameterOwnership;
    using mqb::msvc::ParameterTool;

    verify_current_official_coverage();

    expect(MsvcParameterEngine::classify(ParameterTool::compiler, "/W4").ownership == ParameterOwnership::passthrough,
           "/W4 should be safe compiler passthrough");
    expect(MsvcParameterEngine::classify(ParameterTool::compiler, "/fp:fast").ownership == ParameterOwnership::passthrough,
           "/fp:fast should be safe compiler passthrough");
    expect(MsvcParameterEngine::classify(ParameterTool::compiler, "/std:c++20").ownership == ParameterOwnership::semantic,
           "/std should be semantic compiler policy");
    expect(MsvcParameterEngine::classify(ParameterTool::compiler, "/Iinclude").ownership == ParameterOwnership::passthrough,
           "/I should remain safe compiler passthrough ownership");
    expect(MsvcParameterEngine::classify(ParameterTool::compiler, "/DVALUE=1").ownership == ParameterOwnership::passthrough,
           "/D should remain safe compiler passthrough ownership");
    expect(MsvcParameterEngine::classify(ParameterTool::compiler, "/Foowned.obj").ownership == ParameterOwnership::mqb_owned,
           "/Fo should remain MQB-owned structural routing");
    expect(MsvcParameterEngine::classify(ParameterTool::compiler, "/MP8").ownership == ParameterOwnership::unsupported,
           "/MP should be rejected because MQB owns TU scheduling");
    expect(MsvcParameterEngine::classify(ParameterTool::compiler, "/F1048576").ownership == ParameterOwnership::unsupported,
           "compiler /F should fail closed because MQB links separately; use linker /STACK");
    expect(MsvcParameterEngine::classify(ParameterTool::compiler, "/cgthreads8").ownership == ParameterOwnership::passthrough,
           "compiler prefix routing must not confuse /cgthreads with /c");
    expect(MsvcParameterEngine::classify(ParameterTool::compiler, "/EHsc").ownership == ParameterOwnership::passthrough,
           "compiler prefix routing must not confuse /EHsc with preprocess-only /E");
    expect(MsvcParameterEngine::classify(ParameterTool::compiler, "/NoSuchMsvcOption").rationale.find(unregistered_marker) != std::string::npos,
           "truly unknown compiler options should remain distinguishable from explicitly unsupported options");

    {
        const std::vector<std::string> arguments{"/W4", "/arch:AVX2", "/fp:fast", "/std:c++20", "/MT", "/GL"};
        auto routed = MsvcParameterEngine::route_compiler(arguments);
        expect(routed.has_value(), "valid compiler routing should succeed");
        if (routed) {
            expect(routed->passthrough.size() == 3 && routed->passthrough[0] == "/W4"
                       && routed->passthrough[1] == "/arch:AVX2" && routed->passthrough[2] == "/fp:fast",
                   "safe compiler options should preserve exact order and spelling");
            expect(routed->standard == mqb::CppStandard::cpp20, "C++ /std should normalize to typed standard");
            expect(routed->runtime_library == mqb::RuntimeLibrary::mt, "CRT switch should normalize to typed runtime");
            expect(routed->link_time_code_generation.has_value() && *routed->link_time_code_generation,
                   "/GL should normalize to coupled LTCG enablement");
        }
    }
    {
        const std::vector<std::string> arguments{"/Iinclude", "/DVALUE=1", "/W4"};
        auto routed = MsvcParameterEngine::route_compiler(arguments);
        expect(routed.has_value(), "attached /I and /D routing should succeed");
        if (routed) {
            expect(routed->include_directories.size() == 1
                       && routed->include_directories.front() == std::filesystem::u8path("include"),
                   "attached /I should expose structured include semantics");
            expect(routed->defines.size() == 1 && routed->defines.front() == "VALUE=1",
                   "attached /D should expose structured define semantics");
            expect(routed->passthrough == arguments,
                   "attached preprocessor options must preserve exact raw argv order and spelling");
        }
    }
    {
        const std::vector<std::string> arguments{"/I", "third party", "/D", "NAME=7", "/O2"};
        auto routed = MsvcParameterEngine::route_compiler(arguments);
        expect(routed.has_value(), "split /I and /D routing should succeed");
        if (routed) {
            expect(routed->include_directories.size() == 1
                       && routed->include_directories.front() == std::filesystem::u8path("third party"),
                   "split /I operand should expose structured include semantics");
            expect(routed->defines.size() == 1 && routed->defines.front() == "NAME=7",
                   "split /D operand should expose structured define semantics");
            expect(routed->passthrough == arguments,
                   "split preprocessor options must preserve exact raw token ordering");
        }
    }
    {
        const std::vector<std::string> arguments{"/Iinclude", "/W4"};
        const std::filesystem::path base = std::filesystem::u8path("layer-root");
        auto routed = MsvcParameterEngine::route_compiler(arguments, base);
        expect(routed.has_value(), "path-based native include routing should succeed");
        if (routed) {
            const auto expected = (base / "include").lexically_normal();
            expect(routed->include_directories.size() == 1
                       && routed->include_directories.front() == expected,
                   "native /I metadata should resolve relative to the supplying layer");
            const auto expected_bytes = expected.generic_u8string();
            const std::string expected_text{
                reinterpret_cast<const char*>(expected_bytes.data()), expected_bytes.size()};
            expect(routed->passthrough.size() == 2
                       && routed->passthrough[0] == "/I" + expected_text
                       && routed->passthrough[1] == "/W4",
                   "path normalization may rewrite /I payload but must preserve raw token position");
        }
    }
    {
        const std::vector<std::string> arguments{"/I"};
        auto routed = MsvcParameterEngine::route_compiler(arguments);
        expect(!routed && routed.error().code == ParameterErrorCode::invalid_value,
               "missing /I operand should fail closed");
    }
    {
        const std::vector<std::string> arguments{"/D"};
        auto routed = MsvcParameterEngine::route_compiler(arguments);
        expect(!routed && routed.error().code == ParameterErrorCode::invalid_value,
               "missing /D operand should fail closed");
    }
    {
        const std::vector<std::string> arguments{"/GL-"};
        auto routed = MsvcParameterEngine::route_compiler(arguments);
        expect(routed.has_value() && routed->link_time_code_generation.has_value() && !*routed->link_time_code_generation,
               "/GL- should normalize to typed LTCG disablement");
    }
    {
        const std::vector<std::string> arguments{"/std:c17", "/W3"};
        auto routed = MsvcParameterEngine::route_compiler(arguments);
        expect(routed.has_value(), "C language standard should remain valid raw compiler policy");
        if (routed) {
            expect(!routed->standard, "C language /std mode must not populate CppStandard");
            expect(routed->passthrough.size() == 2 && routed->passthrough.front() == "/std:c17",
                   "C language mode should be preserved verbatim");
        }
    }
    {
        const std::vector<std::string> arguments{"/MD", "/MT"};
        auto routed = MsvcParameterEngine::route_compiler(arguments);
        expect(!routed && routed.error().code == ParameterErrorCode::conflicting_semantic_option,
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
        expect(!routed, "compiler response files should not bypass parameter ownership classification");
    }

    expect(MsvcParameterEngine::classify(ParameterTool::linker, "/stack:8388608").ownership == ParameterOwnership::passthrough,
           "linker registry should be case-insensitive");
    expect(MsvcParameterEngine::classify(ParameterTool::linker, "/OUT:escape.exe").ownership == ParameterOwnership::mqb_owned,
           "/OUT should remain MQB-owned linker routing");
    expect(MsvcParameterEngine::classify(ParameterTool::linker, "/DEBUG:FASTLINK").ownership == ParameterOwnership::passthrough,
           "/DEBUG:FASTLINK ownership is passthrough while toolchain lifecycle admission is deferred");
    {
        const std::vector<std::string> arguments{"/DEBUG:FASTLINK"};
        auto routed = MsvcParameterEngine::route_linker(arguments);
        expect(routed.has_value() && routed->passthrough.size() == 1
                   && routed->passthrough.front() == "/DEBUG:FASTLINK",
               "toolchain-conditional linker options must survive semantic routing for late admission");
    }

    {
        const std::vector<std::string> arguments{"/STACK:8388608", "/DEBUG:FULL", "/machine:x86", "/subsystem:windows", "/ltcg"};
        auto routed = MsvcParameterEngine::route_linker(arguments);
        expect(routed.has_value(), "valid linker routing should succeed");
        if (routed) {
            expect(routed->passthrough.size() == 2 && routed->passthrough[0] == "/STACK:8388608" && routed->passthrough[1] == "/DEBUG:FULL",
                   "safe linker passthrough should preserve exact argv");
            expect(routed->architecture == mqb::Architecture::x86, "/MACHINE should normalize case-insensitively");
            expect(routed->subsystem == mqb::LinkSubsystem::windows, "/SUBSYSTEM should normalize to typed policy");
            expect(routed->link_time_code_generation.has_value() && *routed->link_time_code_generation,
                   "/LTCG should normalize to typed policy");
        }
    }
    {
        const std::vector<std::string> arguments{"/LTCG:INCREMENTAL"};
        auto routed = MsvcParameterEngine::route_linker(arguments);
        expect(!routed, "LTCG variants not representable by the current typed model should fail closed");
    }
    {
        const std::vector<std::string> arguments{"/MACHINE:X64", "/LTCG", "/WX"};
        auto routed = MsvcParameterEngine::route_librarian(arguments);
        expect(routed.has_value(), "valid librarian routing should succeed");
        if (routed) {
            expect(routed->architecture == mqb::Architecture::x64, "librarian /MACHINE should normalize to typed architecture");
            expect(routed->link_time_code_generation.has_value() && *routed->link_time_code_generation,
                   "librarian /LTCG should normalize to typed policy");
            expect(routed->passthrough.size() == 1 && routed->passthrough.front() == "/WX",
                   "safe librarian option should remain passthrough");
        }
    }

    expect(MsvcParameterEngine::classify(ParameterTool::librarian, "/OUT:escape.lib").ownership == ParameterOwnership::mqb_owned,
           "librarian /OUT should remain MQB-owned");
    expect(MsvcParameterEngine::classify(ParameterTool::librarian, "/EXTRACT:member.obj").ownership == ParameterOwnership::unsupported,
           "lib.exe mode-changing operations should be rejected by build routing");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_parameter_engine_tests passed\n";
    return 0;
}
