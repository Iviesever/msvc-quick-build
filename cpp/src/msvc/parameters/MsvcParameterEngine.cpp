#include "mqb/msvc/MsvcParameterEngine.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

namespace mqb::msvc {
namespace {

struct Rule {
    std::string_view prefix;
    ParameterOwnership ownership;
    std::string_view canonical_name;
    std::string_view rationale;
};

[[nodiscard]] std::string upper_ascii(std::string_view value) {
    std::string result{value};
    std::transform(result.begin(), result.end(), result.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return result;
}

[[nodiscard]] std::string_view option_body(const std::string_view argument) noexcept {
    if (argument.size() >= 2 && (argument.front() == '/' || argument.front() == '-')) {
        return argument.substr(1);
    }
    return {};
}

[[nodiscard]] bool starts_with_any(
    const std::string_view value,
    const std::span<const std::string_view> prefixes) noexcept {
    return std::any_of(prefixes.begin(), prefixes.end(), [value](const std::string_view prefix) {
        return value.starts_with(prefix);
    });
}

[[nodiscard]] ParameterClassification classification(
    const ParameterTool tool,
    const ParameterOwnership ownership,
    std::string canonical_name,
    std::string rationale) {
    return ParameterClassification{
        .tool = tool,
        .ownership = ownership,
        .canonical_name = std::move(canonical_name),
        .rationale = std::move(rationale),
    };
}

[[nodiscard]] ParameterError error(
    const ParameterErrorCode code,
    const ParameterTool tool,
    std::string argument,
    std::string message) {
    return ParameterError{
        .code = code,
        .tool = tool,
        .argument = std::move(argument),
        .message = std::move(message),
    };
}

[[nodiscard]] ParameterClassification classify_compiler(const std::string_view argument) {
    if (argument.empty()) {
        return classification(ParameterTool::compiler, ParameterOwnership::unsupported, {}, "empty argument");
    }
    if (argument.front() == '@') {
        return classification(
            ParameterTool::compiler,
            ParameterOwnership::unsupported,
            "@response",
            "response files can hide unclassified compiler options and inputs");
    }
    const std::string_view body = option_body(argument);
    if (body.empty()) {
        return classification(
            ParameterTool::compiler,
            ParameterOwnership::unsupported,
            {},
            "raw compiler arguments must be options, not additional input files");
    }

    if (body == "MD" || body == "MDd" || body == "MT" || body == "MTd") {
        return classification(ParameterTool::compiler, ParameterOwnership::semantic, "/runtime", "normalized to RuntimeLibrary");
    }
    if (body.starts_with("std:")) {
        return classification(ParameterTool::compiler, ParameterOwnership::semantic, "/std", "C++ language modes normalize to CppStandard; C language modes remain validated passthrough");
    }
    if (body == "GL") {
        return classification(ParameterTool::compiler, ParameterOwnership::semantic, "/GL", "normalized to coupled LTCG policy");
    }

    static constexpr std::array owned{
        "c"sv, "Fo"sv, "Fe"sv, "Fd"sv, "Fp"sv,
        "ifcOutput"sv, "sourceDependencies"sv, "scanDependencies"sv,
        "reference"sv, "headerUnit:"sv, "headerName:"sv, "ifcSearchDir"sv,
        "stdIfcDir"sv, "exportHeader"sv, "interface"sv, "TC"sv, "TP"sv,
        "link"sv, "LD"sv, "LDd"sv,
    };
    if (starts_with_any(body, owned)) {
        return classification(
            ParameterTool::compiler,
            ParameterOwnership::mqb_owned,
            "/" + std::string{body},
            "MQB owns compile mode, artifact routing, module topology, or downstream linking");
    }

    static constexpr std::array unsupported{
        "MP"sv, "Yc"sv, "Yu"sv, "Y-"sv,
        "E"sv, "EP"sv, "P"sv, "Zs"sv, "LN"sv,
        "Gm"sv, "GX"sv, "GZ"sv, "Og"sv, "Wp64"sv,
        "experimental:preprocessor"sv, "errorReport"sv,
    };
    if (starts_with_any(body, unsupported)) {
        return classification(
            ParameterTool::compiler,
            ParameterOwnership::unsupported,
            "/" + std::string{body},
            body.starts_with("MP")
                ? "MQB owns TU parallelism; use -j/--jobs instead of cl.exe /MP"
                : body.starts_with("Y")
                    ? "PCH routing is reserved for MQB's first-class PCH pipeline"
                    : "option changes the compile pipeline shape or is deprecated/removed");
    }

    static constexpr std::array passthrough{
        "?"sv, "AI"sv, "analyze"sv, "arch:"sv, "arm64EC"sv, "await"sv,
        "bigobj"sv, "Brepro"sv, "Bt+"sv, "C"sv, "cgthreads"sv, "clr"sv,
        "constexpr:"sv, "D"sv, "diagnostics:"sv, "doc"sv, "dynamicdeopt"sv,
        "EH"sv, "execution-charset:"sv, "experimental:"sv, "external:"sv,
        "F"sv, "FA"sv, "Fa"sv, "favor:"sv, "FC"sv, "FI"sv, "FS"sv,
        "Gd"sv, "GF"sv, "Gh"sv, "GH"sv, "GR"sv, "GS"sv, "guard:"sv,
        "Gw"sv, "Gy"sv, "hotpatch"sv, "I"sv, "J"sv, "JMC"sv,
        "jumptablerdata"sv, "kernel"sv, "openmp"sv, "O1"sv, "O2"sv,
        "Ob"sv, "Od"sv, "Oi"sv, "Os"sv, "Ot"sv, "Ox"sv, "Oy"sv,
        "permissive"sv, "Qpar"sv, "Qspectre"sv, "RTC"sv, "sdl"sv,
        "showIncludes"sv, "source-charset:"sv, "translateInclude"sv,
        "utf-8"sv, "validate-charset"sv, "validateIfcChecksum"sv,
        "volatile:"sv, "W0"sv, "W1"sv, "W2"sv, "W3"sv, "W4"sv,
        "Wall"sv, "wd"sv, "we"sv, "WL"sv, "wo"sv, "Wv:"sv, "WX"sv,
        "Z7"sv, "Za"sv, "Zc:"sv, "Zf"sv, "ZH:"sv, "Zi"sv, "Zl"sv,
        "Zm"sv, "Zo"sv, "Zp"sv, "ZW"sv,
    };
    if (starts_with_any(body, passthrough)) {
        return classification(
            ParameterTool::compiler,
            ParameterOwnership::passthrough,
            "/" + std::string{body},
            "validated compiler option is preserved verbatim in build identity");
    }

    return classification(
        ParameterTool::compiler,
        ParameterOwnership::unsupported,
        "/" + std::string{body},
        "option is not present in MQB's current MSVC compiler registry");
}

[[nodiscard]] ParameterClassification classify_linker(const std::string_view argument) {
    if (argument.empty()) {
        return classification(ParameterTool::linker, ParameterOwnership::unsupported, {}, "empty argument");
    }
    if (argument.front() == '@') {
        return classification(
            ParameterTool::linker,
            ParameterOwnership::unsupported,
            "@RESPONSE",
            "response files can hide untracked linker options and inputs");
    }
    const std::string_view raw_body = option_body(argument);
    if (raw_body.empty()) {
        return classification(
            ParameterTool::linker,
            ParameterOwnership::unsupported,
            {},
            "raw linker arguments must be options; libraries belong to --lib");
    }
    const std::string body = upper_ascii(raw_body);

    if (body.starts_with("MACHINE:") || body.starts_with("SUBSYSTEM:") || body == "LTCG" || body.starts_with("LTCG:")) {
        return classification(ParameterTool::linker, ParameterOwnership::semantic, "/" + body, "normalized to typed linker policy");
    }

    static constexpr std::array owned{
        "OUT:"sv, "DLL"sv, "IMPLIB:"sv, "LIBPATH:"sv, "PDB:"sv, "PDBALTPATH:"sv,
        "PDBSTRIPPED:"sv, "ILK:"sv, "LTCGOUT:"sv, "WHOLEARCHIVE:"sv,
    };
    if (starts_with_any(body, owned)) {
        return classification(
            ParameterTool::linker,
            ParameterOwnership::mqb_owned,
            "/" + body,
            "MQB owns target identity, resolved library inputs, and primary debug/incremental artifacts");
    }

    static constexpr std::array unsupported{
        "DEF:"sv, "ORDER:"sv, "STUB:"sv, "MANIFESTINPUT:"sv,
        "ASSEMBLYMODULE:"sv, "ASSEMBLYRESOURCE:"sv, "ASSEMBLYLINKRESOURCE:"sv,
        "WINMDFILE:"sv, "DEFAULTLIB:"sv,
    };
    if (starts_with_any(body, unsupported)) {
        return classification(
            ParameterTool::linker,
            ParameterOwnership::unsupported,
            "/" + body,
            "option introduces an input that is not yet represented in MQB's freshness graph");
    }

    static constexpr std::array passthrough{
        "?"sv, "ALIGN:"sv, "ALLOWBIND"sv, "ALLOWISOLATION"sv, "APPCONTAINER"sv,
        "ARM64XFUNCTIONPADMINX64:"sv, "ASSEMBLYDEBUG"sv, "BASE:"sv, "CETCOMPAT"sv,
        "CGTHREADS:"sv, "CLRIMAGETYPE:"sv, "CLRLOADEROPTIMIZATION:"sv, "COMMENT:"sv,
        "DEBUG"sv, "DELAYLOAD:"sv, "DEPENDENTLOADFLAG:"sv, "DYNAMICBASE"sv,
        "DYNAMICDEOPT"sv, "ENTRY:"sv, "ERRORREPORT:"sv, "EXPORT:"sv,
        "FASTFAIL"sv, "FASTGENPROFILE"sv, "FIXED"sv, "FORCE"sv, "FUNCTIONPADMIN"sv,
        "GENPROFILE"sv, "GUARD:"sv, "HEAP:"sv, "HIGHENTROPYVA"sv, "INCLUDE:"sv,
        "INCREMENTAL"sv, "INCREMENTAL:NO"sv, "INFERASANLIBS"sv, "INTEGRITYCHECK"sv,
        "KERNEL"sv, "LARGEADDRESSAWARE"sv, "LINKREPRO:"sv, "LINKREPROFULLPATHRSP:"sv,
        "LINKREPROTARGET:"sv, "MAP"sv, "MAP:"sv, "MAPINFO:"sv, "MERGE:"sv,
        "NATVIS:"sv, "NOASSEMBLY"sv, "NODEFAULTLIB"sv, "NODEFAULTLIB:"sv, "NOENTRY"sv,
        "NOEXP"sv, "NOIMPLIB"sv, "NOLOGO"sv, "NXCOMPAT"sv, "OPT:"sv, "PROFILE"sv,
        "RELEASE"sv, "SAFESEH"sv, "SECTION:"sv, "STACK:"sv, "SWAPRUN:"sv,
        "TIME"sv, "TLBID:"sv, "TLBOUT:"sv, "TSAWARE"sv, "USEPROFILE"sv,
        "VERBOSE"sv, "VERSION:"sv, "WINMD"sv, "WX"sv, "WX:NO"sv,
    };
    if (starts_with_any(body, passthrough)) {
        return classification(
            ParameterTool::linker,
            ParameterOwnership::passthrough,
            "/" + body,
            "validated linker option is preserved verbatim in link identity");
    }

    return classification(
        ParameterTool::linker,
        ParameterOwnership::unsupported,
        "/" + body,
        "option is not present in MQB's current MSVC linker registry");
}

[[nodiscard]] ParameterClassification classify_librarian(const std::string_view argument) {
    if (argument.empty()) {
        return classification(ParameterTool::librarian, ParameterOwnership::unsupported, {}, "empty argument");
    }
    if (argument.front() == '@') {
        return classification(
            ParameterTool::librarian,
            ParameterOwnership::unsupported,
            "@RESPONSE",
            "response files can hide untracked librarian modes and inputs");
    }
    const std::string_view raw_body = option_body(argument);
    if (raw_body.empty()) {
        return classification(
            ParameterTool::librarian,
            ParameterOwnership::unsupported,
            {},
            "raw librarian arguments must be options; object membership is owned by MQB");
    }
    const std::string body = upper_ascii(raw_body);

    if (body.starts_with("MACHINE:") || body == "LTCG") {
        return classification(ParameterTool::librarian, ParameterOwnership::semantic, "/" + body, "normalized to typed archive policy");
    }
    if (body.starts_with("OUT:")) {
        return classification(ParameterTool::librarian, ParameterOwnership::mqb_owned, "/OUT", "MQB owns archive output identity and atomic replacement");
    }
    static constexpr std::array unsupported{
        "DEF:"sv, "EXTRACT:"sv, "LIST"sv, "NAME:"sv, "REMOVE:"sv,
    };
    if (starts_with_any(body, unsupported)) {
        return classification(
            ParameterTool::librarian,
            ParameterOwnership::unsupported,
            "/" + body,
            "option changes lib.exe operating mode or archive membership outside MQB's graph");
    }
    static constexpr std::array passthrough{
        "?"sv, "ERRORREPORT:"sv, "EXPORT:"sv, "INCLUDE:"sv, "LIBPATH:"sv,
        "LINKREPRO:"sv, "LINKREPROTARGET:"sv, "NODEFAULTLIB"sv, "NODEFAULTLIB:"sv,
        "NOLOGO"sv, "SUBSYSTEM:"sv, "VERBOSE"sv, "WX"sv, "WX:NO"sv,
    };
    if (starts_with_any(body, passthrough)) {
        return classification(
            ParameterTool::librarian,
            ParameterOwnership::passthrough,
            "/" + body,
            "validated librarian option is preserved verbatim in archive identity");
    }
    return classification(
        ParameterTool::librarian,
        ParameterOwnership::unsupported,
        "/" + body,
        "option is not present in MQB's current MSVC librarian registry");
}

template <typename T>
[[nodiscard]] std::expected<void, ParameterError> assign_semantic(
    std::optional<T>& destination,
    const T value,
    const ParameterTool tool,
    const std::string& argument,
    const std::string_view name) {
    if (destination && *destination != value) {
        return std::unexpected(error(
            ParameterErrorCode::conflicting_semantic_option,
            tool,
            argument,
            "conflicting raw MSVC options select different values for " + std::string{name}));
    }
    destination = value;
    return {};
}

[[nodiscard]] std::expected<void, ParameterError> reject_classification(
    const ParameterClassification& classified,
    const std::string& argument) {
    if (classified.ownership == ParameterOwnership::mqb_owned) {
        return std::unexpected(error(
            ParameterErrorCode::owned_option,
            classified.tool,
            argument,
            "MSVC option '" + argument + "' is MQB-owned: " + classified.rationale));
    }
    if (classified.ownership == ParameterOwnership::unsupported) {
        return std::unexpected(error(
            classified.canonical_name.empty() ? ParameterErrorCode::unknown_option : ParameterErrorCode::unsupported_option,
            classified.tool,
            argument,
            "MSVC option '" + argument + "' is not accepted: " + classified.rationale));
    }
    return {};
}

[[nodiscard]] std::expected<CppStandard, ParameterError> parse_cpp_standard(const std::string& argument) {
    const std::string_view body = option_body(argument);
    const std::string_view value = body.substr(std::string_view{"std:"}.size());
    if (value == "c++14") return CppStandard::cpp14;
    if (value == "c++17") return CppStandard::cpp17;
    if (value == "c++20") return CppStandard::cpp20;
    if (value == "c++23preview") return CppStandard::cpp23;
    if (value == "c++latest") return CppStandard::latest;
    return std::unexpected(error(
        ParameterErrorCode::invalid_value,
        ParameterTool::compiler,
        argument,
        "unsupported /std value; supported C++ modes are c++14, c++17, c++20, c++23preview, and c++latest"));
}

[[nodiscard]] std::expected<Architecture, ParameterError> parse_machine(
    const ParameterTool tool,
    const std::string& argument) {
    const std::string body = upper_ascii(option_body(argument));
    const std::string value = body.substr(std::string{"MACHINE:"}.size());
    if (value == "X86") return Architecture::x86;
    if (value == "X64") return Architecture::x64;
    return std::unexpected(error(
        ParameterErrorCode::invalid_value,
        tool,
        argument,
        "MQB currently supports typed /MACHINE:X86 and /MACHINE:X64 only"));
}

} // namespace

ParameterClassification MsvcParameterEngine::classify(
    const ParameterTool tool,
    const std::string_view argument) {
    switch (tool) {
    case ParameterTool::compiler: return classify_compiler(argument);
    case ParameterTool::linker: return classify_linker(argument);
    case ParameterTool::librarian: return classify_librarian(argument);
    }
    return classification(tool, ParameterOwnership::unsupported, {}, "unknown MSVC tool");
}

std::expected<CompilerParameterRouting, ParameterError>
MsvcParameterEngine::route_compiler(const std::span<const std::string> arguments) {
    CompilerParameterRouting routed;
    routed.passthrough.reserve(arguments.size());
    for (const auto& argument : arguments) {
        if (argument.empty()) {
            return std::unexpected(error(ParameterErrorCode::empty_argument, ParameterTool::compiler, argument, "empty raw compiler argument"));
        }
        const auto classified = classify_compiler(argument);
        if (auto accepted = reject_classification(classified, argument); !accepted) return std::unexpected(accepted.error());
        if (classified.ownership == ParameterOwnership::passthrough) {
            routed.passthrough.push_back(argument);
            continue;
        }

        const std::string_view body = option_body(argument);
        if (body == "MD") {
            if (auto assigned = assign_semantic(routed.runtime_library, RuntimeLibrary::md, ParameterTool::compiler, argument, "runtime library"); !assigned) return std::unexpected(assigned.error());
        } else if (body == "MDd") {
            if (auto assigned = assign_semantic(routed.runtime_library, RuntimeLibrary::mdd, ParameterTool::compiler, argument, "runtime library"); !assigned) return std::unexpected(assigned.error());
        } else if (body == "MT") {
            if (auto assigned = assign_semantic(routed.runtime_library, RuntimeLibrary::mt, ParameterTool::compiler, argument, "runtime library"); !assigned) return std::unexpected(assigned.error());
        } else if (body == "MTd") {
            if (auto assigned = assign_semantic(routed.runtime_library, RuntimeLibrary::mtd, ParameterTool::compiler, argument, "runtime library"); !assigned) return std::unexpected(assigned.error());
        } else if (body == "GL") {
            if (auto assigned = assign_semantic(routed.link_time_code_generation, true, ParameterTool::compiler, argument, "LTCG"); !assigned) return std::unexpected(assigned.error());
        } else if (body.starts_with("std:")) {
            const std::string_view value = body.substr(4);
            if (value == "c11" || value == "c17" || value == "clatest") {
                routed.passthrough.push_back(argument);
                continue;
            }
            auto standard = parse_cpp_standard(argument);
            if (!standard) return std::unexpected(standard.error());
            if (auto assigned = assign_semantic(routed.standard, *standard, ParameterTool::compiler, argument, "C++ standard"); !assigned) return std::unexpected(assigned.error());
        }
    }
    return routed;
}

std::expected<LinkerParameterRouting, ParameterError>
MsvcParameterEngine::route_linker(const std::span<const std::string> arguments) {
    LinkerParameterRouting routed;
    routed.passthrough.reserve(arguments.size());
    for (const auto& argument : arguments) {
        if (argument.empty()) {
            return std::unexpected(error(ParameterErrorCode::empty_argument, ParameterTool::linker, argument, "empty raw linker argument"));
        }
        const auto classified = classify_linker(argument);
        if (auto accepted = reject_classification(classified, argument); !accepted) return std::unexpected(accepted.error());
        if (classified.ownership == ParameterOwnership::passthrough) {
            routed.passthrough.push_back(argument);
            continue;
        }

        const std::string body = upper_ascii(option_body(argument));
        if (body.starts_with("MACHINE:")) {
            auto architecture = parse_machine(ParameterTool::linker, argument);
            if (!architecture) return std::unexpected(architecture.error());
            if (auto assigned = assign_semantic(routed.architecture, *architecture, ParameterTool::linker, argument, "target architecture"); !assigned) return std::unexpected(assigned.error());
        } else if (body.starts_with("SUBSYSTEM:")) {
            const std::string value = body.substr(std::string{"SUBSYSTEM:"}.size());
            if (value == "CONSOLE") {
                if (auto assigned = assign_semantic(routed.subsystem, LinkSubsystem::console, ParameterTool::linker, argument, "subsystem"); !assigned) return std::unexpected(assigned.error());
            } else if (value == "WINDOWS") {
                if (auto assigned = assign_semantic(routed.subsystem, LinkSubsystem::windows, ParameterTool::linker, argument, "subsystem"); !assigned) return std::unexpected(assigned.error());
            } else {
                return std::unexpected(error(
                    ParameterErrorCode::invalid_value,
                    ParameterTool::linker,
                    argument,
                    "MQB currently supports typed /SUBSYSTEM:CONSOLE and /SUBSYSTEM:WINDOWS only"));
            }
        } else if (body == "LTCG:OFF") {
            if (auto assigned = assign_semantic(routed.link_time_code_generation, false, ParameterTool::linker, argument, "LTCG"); !assigned) return std::unexpected(assigned.error());
        } else if (body == "LTCG" || body == "LTCG:INCREMENTAL" || body == "LTCG:NOSTATUS" || body == "LTCG:STATUS") {
            if (auto assigned = assign_semantic(routed.link_time_code_generation, true, ParameterTool::linker, argument, "LTCG"); !assigned) return std::unexpected(assigned.error());
            if (body != "LTCG") routed.passthrough.push_back(argument);
        } else {
            return std::unexpected(error(ParameterErrorCode::invalid_value, ParameterTool::linker, argument, "unsupported semantic linker option value"));
        }
    }
    return routed;
}

std::expected<LibrarianParameterRouting, ParameterError>
MsvcParameterEngine::route_librarian(const std::span<const std::string> arguments) {
    LibrarianParameterRouting routed;
    routed.passthrough.reserve(arguments.size());
    for (const auto& argument : arguments) {
        if (argument.empty()) {
            return std::unexpected(error(ParameterErrorCode::empty_argument, ParameterTool::librarian, argument, "empty raw librarian argument"));
        }
        const auto classified = classify_librarian(argument);
        if (auto accepted = reject_classification(classified, argument); !accepted) return std::unexpected(accepted.error());
        if (classified.ownership == ParameterOwnership::passthrough) {
            routed.passthrough.push_back(argument);
            continue;
        }
        const std::string body = upper_ascii(option_body(argument));
        if (body.starts_with("MACHINE:")) {
            auto architecture = parse_machine(ParameterTool::librarian, argument);
            if (!architecture) return std::unexpected(architecture.error());
            if (auto assigned = assign_semantic(routed.architecture, *architecture, ParameterTool::librarian, argument, "target architecture"); !assigned) return std::unexpected(assigned.error());
        } else if (body == "LTCG") {
            if (auto assigned = assign_semantic(routed.link_time_code_generation, true, ParameterTool::librarian, argument, "LTCG"); !assigned) return std::unexpected(assigned.error());
        }
    }
    return routed;
}

std::string_view to_string(const ParameterTool tool) noexcept {
    switch (tool) {
    case ParameterTool::compiler: return "compiler";
    case ParameterTool::linker: return "linker";
    case ParameterTool::librarian: return "librarian";
    }
    return "unknown";
}

std::string_view to_string(const ParameterOwnership ownership) noexcept {
    switch (ownership) {
    case ParameterOwnership::mqb_owned: return "mqb-owned";
    case ParameterOwnership::semantic: return "semantic";
    case ParameterOwnership::passthrough: return "passthrough";
    case ParameterOwnership::unsupported: return "unsupported";
    }
    return "unknown";
}

} // namespace mqb::msvc
