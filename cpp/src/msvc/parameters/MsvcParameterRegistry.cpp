#include "MsvcParameterRegistry.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

namespace mqb::msvc::detail {
namespace {

using namespace std::string_view_literals;
constexpr std::string_view kUnregisteredRationale =
    "option is not present in MQB's current MSVC parameter registry";

template <std::size_t N>
[[nodiscard]] bool contains_exact(const std::string_view value, const std::array<std::string_view, N>& candidates) noexcept {
    return std::find(candidates.begin(), candidates.end(), value) != candidates.end();
}

template <std::size_t N>
[[nodiscard]] bool starts_with_any(const std::string_view value, const std::array<std::string_view, N>& prefixes) noexcept {
    return std::any_of(prefixes.begin(), prefixes.end(), [value](const std::string_view prefix) { return value.starts_with(prefix); });
}

[[nodiscard]] ParameterClassification classified(const ParameterTool tool, const ParameterOwnership ownership, std::string canonical_name, std::string rationale) {
    return ParameterClassification{.tool = tool, .ownership = ownership, .canonical_name = std::move(canonical_name), .rationale = std::move(rationale)};
}

[[nodiscard]] ParameterClassification unregistered(const ParameterTool tool, const std::string_view body) {
    return classified(tool, ParameterOwnership::unsupported, body.empty() ? std::string{} : "/" + std::string{body}, std::string{kUnregisteredRationale});
}

[[nodiscard]] ParameterClassification compiler_unsupported(const std::string_view body, std::string rationale) {
    return classified(ParameterTool::compiler, ParameterOwnership::unsupported, "/" + std::string{body}, std::move(rationale));
}
[[nodiscard]] ParameterClassification linker_unsupported(const std::string_view body, std::string rationale) {
    return classified(ParameterTool::linker, ParameterOwnership::unsupported, "/" + std::string{body}, std::move(rationale));
}
[[nodiscard]] ParameterClassification librarian_unsupported(const std::string_view body, std::string rationale) {
    return classified(ParameterTool::librarian, ParameterOwnership::unsupported, "/" + std::string{body}, std::move(rationale));
}

} // namespace

std::string upper_ascii(std::string_view value) {
    std::string result{value};
    std::transform(result.begin(), result.end(), result.begin(), [](const unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return result;
}

std::string_view option_body(const std::string_view argument) noexcept {
    if (argument.size() >= 2 && (argument.front() == '/' || argument.front() == '-')) return argument.substr(1);
    return {};
}

ParameterClassification classify_compiler_parameter(const std::string_view argument) {
    if (argument.empty()) return unregistered(ParameterTool::compiler, {});
    if (argument.front() == '@') return compiler_unsupported("@response", "response files can hide unclassified compiler options and inputs");
    const std::string_view body = option_body(argument);
    if (body.empty()) return unregistered(ParameterTool::compiler, {});

    if (body == "MD" || body == "MDd" || body == "MT" || body == "MTd") {
        return classified(ParameterTool::compiler, ParameterOwnership::semantic, "/runtime", "normalized to RuntimeLibrary");
    }
    if (body == "std:c11" || body == "std:c17" || body == "std:clatest") {
        return classified(ParameterTool::compiler, ParameterOwnership::passthrough, "/" + std::string{body}, "C language standard mode is validated and preserved verbatim in compile identity");
    }
    if (body.starts_with("std:")) {
        return classified(ParameterTool::compiler, ParameterOwnership::semantic, "/std", "C++ language mode normalizes to CppStandard");
    }
    if (body == "GL" || body == "GL-") {
        return classified(ParameterTool::compiler, ParameterOwnership::semantic, "/GL", "normalized to coupled LTCG policy");
    }

    static constexpr std::array owned_exact{
        "c"sv, "exportHeader"sv, "interface"sv, "internalPartition"sv, "LD"sv, "LDd"sv, "link"sv, "TC"sv, "TP"sv,
    };
    static constexpr std::array owned_prefix{
        "Fd"sv, "Fe"sv, "Fi"sv, "Fm"sv, "Fo"sv, "Fp"sv, "headerName"sv, "headerUnit"sv, "ifcMap"sv, "ifcOutput"sv, "ifcSearchDir"sv,
        "reference"sv, "scanDependencies"sv, "sourceDependencies"sv, "stdIfcDir"sv, "Tc"sv, "Tp"sv,
    };
    if (contains_exact(body, owned_exact) || starts_with_any(body, owned_prefix)) {
        return classified(ParameterTool::compiler, ParameterOwnership::mqb_owned, "/" + std::string{body}, "MQB owns target/TU shape, primary artifact routing, module topology, or dependency metadata");
    }

    if (body == "MP" || body.starts_with("MP")) return compiler_unsupported(body, "MQB owns TU parallelism; use -j/--jobs instead of cl.exe /MP");
    if (body == "Y-" || body.starts_with("Yc") || body.starts_with("Yu") || body.starts_with("Yl")) {
        return compiler_unsupported(body, "PCH selection and artifacts are reserved for MQB's first-class PCH pipeline");
    }
    if (body == "FU" || body.starts_with("FU")) {
        return compiler_unsupported(body, "forced metadata references introduce an external file input that is not represented in MQB's compile freshness graph");
    }
    if (body == "external:env" || body.starts_with("external:env:")) {
        return compiler_unsupported(body, "environment-backed external include directories can change header search without entering MQB's compile identity or discovery model; use explicit /external:I or -I paths instead");
    }
    if (body == "fsanitize=kernel-address") {
        return compiler_unsupported(body, "Kernel AddressSanitizer is a WDK kernel-driver pipeline that requires x64 kernel target policy and a target-OS-matched Windows SDK/KASan compatibility library; MQB currently models only native exe/dll/static targets");
    }
    if (body == "fno-sanitize-address-vcasan-lib") {
        return classified(ParameterTool::compiler, ParameterOwnership::passthrough, "/fno-sanitize-address-vcasan-lib", "VCAsan default-library opt-out is preserved verbatim in compile identity and projected into ASan link freshness policy");
    }
    if (body == "openmp:llvm") {
        return compiler_unsupported(body, "LLVM OpenMP selects the separate experimental libomp runtime whose libraries/DLL deployment are not yet represented in MQB's link/runtime ownership model; use classic /openmp or /openmp:experimental for now");
    }
    if (body == "openmp" || body == "openmp:experimental" || body == "openmp-") {
        return classified(ParameterTool::compiler, ParameterOwnership::passthrough, "/" + std::string{body}, "classic MSVC OpenMP mode is preserved verbatim in compile identity and projected into vcomp/vcompd link freshness policy");
    }
    if (body == "experimental:log" || body.starts_with("experimental:log")) {
        return compiler_unsupported(body, "option creates a diagnostic log artifact that is not represented in MQB's cache/artifact graph");
    }
    if (body == "analyze" || body.starts_with("analyze:")) {
        return compiler_unsupported(body, "code analysis creates diagnostic artifacts and may introduce plugin/ruleset file inputs that are not represented in MQB's compile freshness/artifact graph; /analyze- remains admissible as an explicit disable switch");
    }
    if (body == "F" || (body.size() > 1 && body.front() == 'F' && std::isdigit(static_cast<unsigned char>(body[1])) != 0)) {
        return compiler_unsupported(body, "cl.exe /F controls linker stack size, but MQB compiles with /c and owns a separate link.exe invocation; use linker /STACK instead");
    }
    if (body == "FA" || body.starts_with("Fa") || body.starts_with("FR") || body.starts_with("Fr") || body.starts_with("Ft") || body.starts_with("Fx") || body == "doc" || body.starts_with("doc:")) {
        return compiler_unsupported(body, "option creates a secondary output artifact that is not yet represented in MQB's cache/artifact graph");
    }
    if (body == "Zi" || body == "ZI") {
        return compiler_unsupported(body, "compiler Program Database debug information creates a .pdb artifact that is not yet represented in MQB's compile cache/artifact graph; use /Z7 until compiler PDB output becomes first-class");
    }
    if (body == "arm64EC") return compiler_unsupported(body, "MQB's current Architecture model supports x86/x64 only; ARM64EC requires a first-class toolchain/ABI policy");
    if (body == "kernel") return compiler_unsupported(body, "kernel-mode compilation is coupled to linker target policy that MQB does not model yet");
    if (body == "ZW") return compiler_unsupported(body, "Windows Runtime compilation changes the target pipeline and is not modeled by MQB");
    if (body == "clr" || body.starts_with("clr:")) return compiler_unsupported(body, "CLR compilation changes the compiler/linker pipeline and is not modeled by MQB");
    if (body == "experimental:module") return compiler_unsupported(body, "MQB owns modern named-module/header-unit topology; the legacy experimental module mode is not admitted");

    static constexpr std::array deprecated_exact{
        "Ge"sv, "GZ"sv, "H"sv, "Og"sv, "QIfist"sv, "V"sv, "Wp64"sv, "Yd"sv, "Zc:trigraphs"sv, "Ze"sv, "Zg"sv,
    };
    static constexpr std::array deprecated_prefix{"errorReport"sv, "experimental:preprocessor"sv, "Gm"sv, "GX"sv};
    if (contains_exact(body, deprecated_exact) || starts_with_any(body, deprecated_prefix)) {
        return compiler_unsupported(body, "option is deprecated, obsolete, or removed in current MSVC toolchains");
    }

    static constexpr std::array pipeline_exact{"E"sv, "EP"sv, "LN"sv, "P"sv, "Zs"sv};
    if (contains_exact(body, pipeline_exact)) return compiler_unsupported(body, "option replaces normal object compilation or changes the output kind outside MQB's target pipeline");

    static constexpr std::array passthrough_exact{
        "?"sv, "HELP"sv, "analyze-"sv, "bigobj"sv, "Brepro"sv, "Bt+"sv, "C"sv, "dynamicdeopt"sv, "fastfail"sv,
        "FC"sv, "FS"sv, "GA"sv, "GF"sv, "GH"sv, "Gh"sv, "GT"sv, "Gv"sv, "Gz"sv, "homeparams"sv, "hotpatch"sv,
        "J"sv, "JMC"sv, "jumptablerdata"sv, "nologo"sv, "O1"sv, "O2"sv, "Od"sv, "options:strict"sv, "Os"sv, "Ot"sv, "Ox"sv, "Oy"sv,
        "PD"sv, "permissive"sv, "permissive-"sv, "PH"sv, "presetPadding"sv, "Qfast_transcendentals"sv, "Qimprecise_fwaits"sv,
        "QIntel-jcc-erratum"sv, "Qsafe_fp_loads"sv, "Qspectre-load"sv, "Qspectre-load-cf"sv, "sdl"sv, "sdl-"sv, "showIncludes"sv,
        "translateInclude"sv, "u"sv, "utf-8"sv, "validate-charset"sv, "vlen"sv, "vmb"sv, "vmg"sv, "vmm"sv, "vms"sv, "vmv"sv,
        "volatileMetadata"sv, "w"sv, "W0"sv, "W1"sv, "W2"sv, "W3"sv, "W4"sv, "Wall"sv, "WL"sv, "WX"sv, "WX-"sv, "X"sv,
        "Z7"sv, "Za"sv, "Zf"sv, "Zl"sv,
    };
    static constexpr std::array passthrough_prefix{
        "AI"sv, "arch"sv, "await"sv, "cgthreads"sv, "constexpr:"sv, "D"sv, "diagnostics"sv, "EH"sv,
        "execution-charset"sv, "external:"sv, "favor:"sv, "feature"sv, "FI"sv, "forceInterlockedFunctions"sv,
        "fp:"sv, "fpcvt:"sv, "fsanitize"sv, "Gd"sv, "Gr"sv, "GR"sv, "GS"sv, "Gs"sv, "Gu"sv, "guard:"sv, "Gw"sv, "Gy"sv,
        "I"sv, "Ob"sv, "Oi"sv, "Qpar"sv, "Qpar-report:"sv, "Qspectre"sv, "Qvec-report:"sv, "RTC"sv,
        "source-charset"sv, "U"sv, "vd"sv, "vm"sv, "volatile:"sv, "w1"sv, "w2"sv, "w3"sv, "w4"sv, "wd"sv, "we"sv, "wo"sv,
        "Wv:"sv, "Zc:"sv, "ZH:"sv, "Zm"sv, "Zo"sv, "Zp"sv,
    };
    if (contains_exact(body, passthrough_exact) || starts_with_any(body, passthrough_prefix)) {
        return classified(ParameterTool::compiler, ParameterOwnership::passthrough, "/" + std::string{body}, "validated compiler option is preserved verbatim in compile identity");
    }
    return unregistered(ParameterTool::compiler, body);
}

ParameterClassification classify_linker_parameter(const std::string_view argument) {
    if (argument.empty()) return unregistered(ParameterTool::linker, {});
    if (argument.front() == '@') return linker_unsupported("@RESPONSE", "response files can hide untracked linker options and input files");
    const std::string_view raw_body = option_body(argument);
    if (raw_body.empty()) return unregistered(ParameterTool::linker, {});
    const std::string body = upper_ascii(raw_body);

    if (body.starts_with("MACHINE:") || body.starts_with("SUBSYSTEM:") || body == "LTCG" || body == "LTCG:OFF") {
        return classified(ParameterTool::linker, ParameterOwnership::semantic, "/" + body, "normalized to typed linker policy");
    }

    static constexpr std::array owned_exact{"DLL"sv};
    static constexpr std::array owned_prefix{"ILK:"sv, "IMPLIB:"sv, "LIBPATH:"sv, "LTCGOUT:"sv, "OUT:"sv, "PDB:"sv};
    if (contains_exact(std::string_view{body}, owned_exact) || starts_with_any(std::string_view{body}, owned_prefix)) {
        return classified(ParameterTool::linker, ParameterOwnership::mqb_owned, "/" + body, "MQB owns target identity, primary link artifacts, or structured library search inputs");
    }

    if (body == "DEBUG:FASTLINK") {
        return classified(ParameterTool::linker, ParameterOwnership::passthrough, "/DEBUG:FASTLINK", "availability is toolchain-version conditional and is validated after MSVC discovery");
    }
    if (body == "LTCG:INCREMENTAL" || body == "LTCG:NOSTATUS" || body == "LTCG:STATUS") return linker_unsupported(body, "current MQB LTCG policy is boolean and cannot preserve this /LTCG mode yet");
    if (body == "DRIVER" || body.starts_with("DRIVER:")) return linker_unsupported(body, "kernel-driver target shape requires a first-class MQB target policy");
    if (body == "KERNEL") return linker_unsupported(body, "kernel-mode linking is coupled to compiler policy that MQB does not model yet");
    if (body.starts_with("LINKREPRO:") || body.starts_with("LINKREPROFULLPATHRSP:") || body.starts_with("LINKREPROTARGET:")) {
        return linker_unsupported(body, "link repro generation creates diagnostic artifact files/directories outside MQB's link artifact graph");
    }

    static constexpr std::array deprecated_exact{"POGOSAFEMODE"sv};
    static constexpr std::array deprecated_prefix{"ERRORREPORT"sv};
    if (contains_exact(std::string_view{body}, deprecated_exact) || starts_with_any(std::string_view{body}, deprecated_prefix)) return linker_unsupported(body, "option is deprecated, obsolete, or removed in current MSVC toolchains");

    static constexpr std::array graph_input_prefix{
        "ASSEMBLYLINKRESOURCE:"sv, "ASSEMBLYMODULE:"sv, "ASSEMBLYRESOURCE:"sv, "KEYFILE:"sv,
        "NATVIS:"sv, "SOURCELINK:"sv, "SPD:"sv, "SPDEMBED:"sv, "SPDIN:"sv, "WINMDKEYFILE:"sv,
    };
    static constexpr std::array graph_output_prefix{"IDLOUT:"sv, "MANIFESTFILE:"sv, "PDBSTRIPPED:"sv, "PGD:"sv, "TLBOUT:"sv, "WINMDFILE:"sv};
    if (starts_with_any(std::string_view{body}, graph_input_prefix)) return linker_unsupported(body, "option introduces a file input that is not yet represented in MQB's link freshness graph");
    if (starts_with_any(std::string_view{body}, graph_output_prefix)) return linker_unsupported(body, "option creates a secondary output artifact that is not yet represented in MQB's link artifact graph");

    static constexpr std::array unsupported_managed_prefix{"CLRSUPPORTLASTERROR"sv, "CLRTHREADATTRIBUTE:"sv, "CLRUNMANAGEDCODECHECK"sv, "KEYCONTAINER:"sv, "MIDL:"sv, "WINMDKEYCONTAINER:"sv};
    static constexpr std::array unsupported_managed_exact{"WINMD"sv, "WINMDDELAYSIGN"sv};
    if (contains_exact(std::string_view{body}, unsupported_managed_exact) || starts_with_any(std::string_view{body}, unsupported_managed_prefix)) return linker_unsupported(body, "managed/metadata/signing policy is outside MQB's current native target model");

    static constexpr std::array unsupported_pgo_exact{"FASTGENPROFILE"sv, "GENPROFILE"sv, "SPGO"sv, "USEPROFILE"sv};
    if (contains_exact(std::string_view{body}, unsupported_pgo_exact)) return linker_unsupported(body, "profile-guided optimization artifacts are not yet represented in MQB's build graph");

    static constexpr std::array passthrough_exact{
        "?"sv, "ALLOWBIND"sv, "ALLOWISOLATION"sv, "APPCONTAINER"sv, "ASSEMBLYDEBUG"sv, "CETCOMPAT"sv, "DEBUG"sv, "DEBUG:FULL"sv, "DEBUG:NONE"sv,
        "DELAYSIGN"sv, "DYNAMICBASE"sv, "DYNAMICDEOPT"sv, "FIXED"sv, "FORCE"sv, "FUNCTIONPADMIN"sv, "HIGHENTROPYVA"sv, "IGNOREIDL"sv,
        "INCREMENTAL"sv, "INCREMENTAL:NO"sv, "INFERASANLIBS"sv, "INFERASANLIBS:NO"sv, "INTEGRITYCHECK"sv, "LARGEADDRESSAWARE"sv, "MANIFEST"sv, "MAP"sv,
        "MANIFEST:NO"sv, "NOASSEMBLY"sv, "NODEFAULTLIB"sv, "NOENTRY"sv, "NOFUNCTIONPADSECTION"sv, "NOLOGO"sv, "NXCOMPAT"sv,
        "PROFILE"sv, "RELEASE"sv, "SAFESEH"sv, "TIME"sv, "TSAWARE"sv, "VERBOSE"sv, "WHOLEARCHIVE"sv, "WX"sv, "WX:NO"sv,
    };
    static constexpr std::array passthrough_prefix{
        "ALIGN:"sv, "ALLOWBIND:"sv, "ALLOWISOLATION:"sv, "APPCONTAINER:"sv, "ARM64XFUNCTIONPADMINX64:"sv, "ASSEMBLYDEBUG:"sv, "BASE:"sv,
        "CETCOMPAT:"sv, "CGTHREADS:"sv, "CLRIMAGETYPE:"sv, "COMMENT:"sv, "DEBUGTYPE:"sv, "DEF:"sv, "DEFAULTLIB:"sv, "DELAY:"sv, "DELAYSIGN:"sv, "DELAYLOAD:"sv,
        "DEPENDENTLOADFLAG:"sv, "DYNAMICBASE:"sv, "ENTRY:"sv, "EXPORT:"sv, "FILEALIGN:"sv, "FIXED:"sv, "FORCE:"sv, "GUARD:"sv,
        "HEAP:"sv, "HIGHENTROPYVA:"sv, "IGNORE:"sv, "INCLUDE:"sv, "LARGEADDRESSAWARE:"sv,
        "MANIFEST:"sv, "MANIFESTDEPENDENCY:"sv, "MANIFESTINPUT:"sv, "MANIFESTUAC:"sv, "MAP:"sv, "MAPINFO:"sv, "MERGE:"sv,
        "NODEFAULTLIB:"sv, "NXCOMPAT:"sv, "OPT:"sv, "ORDER:"sv, "PDBALTPATH:"sv, "SAFESEH:"sv, "SECTION:"sv, "STACK:"sv, "STUB:"sv, "SWAPRUN:"sv,
        "TIMESTAMP:"sv, "TLBID:"sv, "TSAWARE:"sv, "VERSION:"sv, "WHOLEARCHIVE:"sv,
    };
    if (contains_exact(std::string_view{body}, passthrough_exact) || starts_with_any(std::string_view{body}, passthrough_prefix)) {
        return classified(ParameterTool::linker, ParameterOwnership::passthrough, "/" + body,
            body.starts_with("WHOLEARCHIVE:")
                ? "path-bearing /WHOLEARCHIVE is preserved in linker argv and its resolved library is tracked through MQB's generic link file-input freshness graph"
                : body == "INFERASANLIBS" || body == "INFERASANLIBS:NO"
                    ? "AddressSanitizer runtime inference mode is preserved in linker argv; when inference is enabled MQB tracks the resolved runtime libraries as link freshness inputs"
                    : body.starts_with("DEFAULTLIB:")
                        ? "user-declared default library remains in raw LINK argv while MQB resolves the effective declaration separately for freshness without changing library search priority"
                        : body.starts_with("DEF:")
                            ? "module-definition input is preserved in linker argv and tracked through MQB's generic link file-input freshness graph"
                            : body.starts_with("ORDER:")
                                ? "function-order input is preserved in linker argv, tracked through MQB's generic link file-input freshness graph, and requires non-incremental linking when LINK runs"
                                : body.starts_with("STUB:")
                                    ? "MS-DOS stub executable is preserved in linker argv and tracked through MQB's generic link file-input freshness graph"
                                    : body.starts_with("MANIFESTINPUT:")
                                        ? "manifest input is preserved in linker argv and cumulatively tracked through MQB's generic link file-input freshness graph"
                                        : body == "MAP" || body.starts_with("MAP:")
                                            ? "mapfile output is preserved in linker argv and tracked through MQB's link side-output repair graph"
                                            : "validated linker option is preserved verbatim in link identity");
    }
    return unregistered(ParameterTool::linker, body);
}

ParameterClassification classify_librarian_parameter(const std::string_view argument) {
    if (argument.empty()) return unregistered(ParameterTool::librarian, {});
    if (argument.front() == '@') return librarian_unsupported("@RESPONSE", "response files can hide untracked librarian options and input files");
    const std::string_view raw_body = option_body(argument);
    if (raw_body.empty()) return unregistered(ParameterTool::librarian, {});
    const std::string body = upper_ascii(raw_body);

    if (body.starts_with("MACHINE:") || body == "LTCG") return classified(ParameterTool::librarian, ParameterOwnership::semantic, "/" + body, "normalized to typed archive policy");
    if (body.starts_with("OUT:")) return classified(ParameterTool::librarian, ParameterOwnership::mqb_owned, "/OUT", "MQB owns archive output identity and atomic replacement");
    if (body == "LIST" || body.starts_with("DEF:") || body.starts_with("EXTRACT:") || body.starts_with("NAME:") || body.starts_with("REMOVE:")) return librarian_unsupported(body, "option changes lib.exe operating mode or archive membership outside MQB's build graph");
    if (body.starts_with("ERRORREPORT")) return librarian_unsupported(body, "option is deprecated in current MSVC toolchains");
    if (body.starts_with("LINKREPRO:") || body.starts_with("LINKREPROTARGET:")) {
        return librarian_unsupported(body, "library repro generation creates diagnostic artifact files/directories outside MQB's archive artifact graph");
    }

    static constexpr std::array passthrough_exact{"?"sv, "NODEFAULTLIB"sv, "NOLOGO"sv, "VERBOSE"sv, "WX"sv, "WX:NO"sv};
    static constexpr std::array passthrough_prefix{"EXPORT:"sv, "INCLUDE:"sv, "LIBPATH:"sv, "NODEFAULTLIB:"sv, "SUBSYSTEM:"sv};
    if (contains_exact(std::string_view{body}, passthrough_exact) || starts_with_any(std::string_view{body}, passthrough_prefix)) {
        return classified(ParameterTool::librarian, ParameterOwnership::passthrough, "/" + body, "validated librarian option is preserved verbatim in archive identity");
    }
    return unregistered(ParameterTool::librarian, body);
}

bool is_unregistered(const ParameterClassification& classification) noexcept {
    return classification.rationale == kUnregisteredRationale;
}

} // namespace mqb::msvc::detail
