#include <array>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/msvc/MsvcParameterEngine.hpp"

namespace {

using mqb::msvc::MsvcParameterEngine;
using mqb::msvc::ParameterErrorCode;
using mqb::msvc::ParameterOwnership;
using mqb::msvc::ParameterTool;

struct SemanticVariantCase {
    ParameterTool tool;
    std::string_view family;
    std::string_view variant;
    std::string_view risk;
    ParameterOwnership expected;
    std::string_view rationale_contains;
    std::string_view behavioral_gate;
    bool require_route_rejection{false};
};

constexpr std::string_view kDynamicDebuggingRationale =
    "Dynamic Debugging introduces cross-stage policy and secondary artifacts that are not yet represented in MQB's build/cache graph.";

constexpr std::array kCases{
    SemanticVariantCase{ParameterTool::compiler, "dynamic-debugging", "/dynamicdeopt", "cross-stage", ParameterOwnership::unsupported, kDynamicDebuggingRationale, "route-fail-closed", true},
    SemanticVariantCase{ParameterTool::compiler, "dynamic-debugging", "/dynamicdeopt:sync", "cross-stage", ParameterOwnership::unsupported, kDynamicDebuggingRationale, "route-fail-closed", true},
    SemanticVariantCase{ParameterTool::compiler, "dynamic-debugging", "/dynamicdeopt:suffix", "artifact-producing", ParameterOwnership::unsupported, kDynamicDebuggingRationale, "route-fail-closed", true},
    SemanticVariantCase{ParameterTool::linker, "dynamic-debugging", "/DYNAMICDEOPT", "cross-stage", ParameterOwnership::unsupported, kDynamicDebuggingRationale, "route-fail-closed", true},
    SemanticVariantCase{ParameterTool::linker, "dynamic-debugging", "/DYNAMICDEOPT:SYNC", "cross-stage", ParameterOwnership::unsupported, kDynamicDebuggingRationale, "route-fail-closed", true},
    SemanticVariantCase{ParameterTool::linker, "dynamic-debugging", "/DYNAMICDEOPT:SUFFIX=.mqb_alt", "artifact-producing", ParameterOwnership::unsupported, kDynamicDebuggingRationale, "route-fail-closed", true},
    SemanticVariantCase{ParameterTool::librarian, "dynamic-debugging", "/dynamicdeopt", "cross-stage", ParameterOwnership::unsupported, kDynamicDebuggingRationale, "route-fail-closed", true},
    SemanticVariantCase{ParameterTool::librarian, "dynamic-debugging", "/dynamicdeopt:sync", "cross-stage", ParameterOwnership::unsupported, kDynamicDebuggingRationale, "route-fail-closed", true},
    SemanticVariantCase{ParameterTool::compiler, "response-file", "@compile.rsp", "response-file-bearing", ParameterOwnership::unsupported, "response files can hide", "classifier-policy"},
    SemanticVariantCase{ParameterTool::linker, "response-file", "@link.rsp", "response-file-bearing", ParameterOwnership::unsupported, "response files can hide", "classifier-policy"},
    SemanticVariantCase{ParameterTool::librarian, "response-file", "@archive.rsp", "response-file-bearing", ParameterOwnership::unsupported, "response files can hide", "classifier-policy"},
    SemanticVariantCase{ParameterTool::compiler, "compiler-pdb", "/Zi", "artifact-producing", ParameterOwnership::unsupported, ".pdb artifact", "compiler-pdb-ownership"},
    SemanticVariantCase{ParameterTool::compiler, "assembly-listing", "/FA", "artifact-producing", ParameterOwnership::unsupported, "secondary output artifact", "classifier-policy"},
    SemanticVariantCase{ParameterTool::linker, "mapfile", "/MAP:program.map", "artifact-producing", ParameterOwnership::passthrough, "side-output repair graph", "tests/native/verify_link_side_output_repair.ps1"},
    SemanticVariantCase{ParameterTool::linker, "stripped-pdb", "/PDBSTRIPPED:public.pdb", "artifact-producing", ParameterOwnership::unsupported, "secondary output artifact", "classifier-policy"},
    SemanticVariantCase{ParameterTool::linker, "pgo-database", "/PGD:profile.pgd", "artifact-producing", ParameterOwnership::unsupported, "secondary output artifact", "classifier-policy"},
    SemanticVariantCase{ParameterTool::linker, "link-repro", "/LINKREPRO:repro", "artifact-producing", ParameterOwnership::unsupported, "diagnostic artifact", "tests/native/verify_link_side_output_repair.ps1"},
    SemanticVariantCase{ParameterTool::librarian, "link-repro", "/LINKREPRO:repro", "artifact-producing", ParameterOwnership::unsupported, "diagnostic artifact", "tests/native/verify_link_side_output_repair.ps1"},
    SemanticVariantCase{ParameterTool::compiler, "forced-metadata", "/FUmetadata.winmd", "file-input-bearing", ParameterOwnership::unsupported, "external file input", "classifier-policy"},
    SemanticVariantCase{ParameterTool::compiler, "external-env", "/external:env:INCLUDE", "file-input-bearing", ParameterOwnership::unsupported, "header search", "tests/native/verify_external_env_ownership.ps1"},
    SemanticVariantCase{ParameterTool::linker, "module-definition", "/DEF:exports.def", "file-input-bearing", ParameterOwnership::passthrough, "file-input freshness graph", "linker-file-input-tests"},
    SemanticVariantCase{ParameterTool::linker, "function-order", "/ORDER:@order.txt", "file-input-bearing", ParameterOwnership::passthrough, "file-input freshness graph", "linker-file-input-tests"},
    SemanticVariantCase{ParameterTool::linker, "msdos-stub", "/STUB:stub.exe", "file-input-bearing", ParameterOwnership::passthrough, "file-input freshness graph", "linker-file-input-tests"},
    SemanticVariantCase{ParameterTool::linker, "manifest-input", "/MANIFESTINPUT:app.manifest", "file-input-bearing", ParameterOwnership::passthrough, "file-input freshness graph", "linker-file-input-tests"},
    SemanticVariantCase{ParameterTool::linker, "whole-archive", "/WHOLEARCHIVE:whole.lib", "file-input-bearing", ParameterOwnership::passthrough, "file-input freshness graph", "wholearchive-freshness"},
    SemanticVariantCase{ParameterTool::linker, "source-link", "/SOURCELINK:source.json", "file-input-bearing", ParameterOwnership::unsupported, "file input", "classifier-policy"},
    SemanticVariantCase{ParameterTool::linker, "base-response", "/BASE:@addresses.txt,main", "response-file-bearing", ParameterOwnership::passthrough, "validated linker option", "base-response-freshness"},
    SemanticVariantCase{ParameterTool::compiler, "ltcg", "/GL", "cross-stage", ParameterOwnership::semantic, "coupled LTCG policy", "ltcg-policy"},
    SemanticVariantCase{ParameterTool::linker, "ltcg", "/LTCG", "cross-stage", ParameterOwnership::semantic, "typed linker policy", "ltcg-policy"},
    SemanticVariantCase{ParameterTool::librarian, "ltcg", "/LTCG", "cross-stage", ParameterOwnership::semantic, "typed archive policy", "archive-ltcg-policy"},
    SemanticVariantCase{ParameterTool::compiler, "address-sanitizer", "/fsanitize=address", "cross-stage", ParameterOwnership::passthrough, "compile identity", "tests/native/verify_asan_link_policy.ps1"},
    SemanticVariantCase{ParameterTool::compiler, "libfuzzer", "/fsanitize=fuzzer", "cross-stage", ParameterOwnership::passthrough, "compile identity", "tests/native/verify_fuzzer_link_policy.ps1"},
    SemanticVariantCase{ParameterTool::compiler, "openmp", "/openmp", "cross-stage", ParameterOwnership::passthrough, "vcomp/vcompd", "tests/native/verify_openmp_runtime_policy.ps1"},
    SemanticVariantCase{ParameterTool::compiler, "openmp-llvm", "/openmp:llvm", "cross-stage", ParameterOwnership::unsupported, "libomp runtime", "tests/native/verify_openmp_runtime_policy.ps1"},
    SemanticVariantCase{ParameterTool::linker, "asan-inference", "/INFERASANLIBS:NO", "mode-switching", ParameterOwnership::passthrough, "AddressSanitizer runtime inference", "tests/native/verify_asan_link_policy.ps1"},
    SemanticVariantCase{ParameterTool::compiler, "preprocess-only", "/E", "pipeline-changing", ParameterOwnership::unsupported, "replaces normal object compilation", "classifier-policy"},
    SemanticVariantCase{ParameterTool::compiler, "clr", "/clr", "pipeline-changing", ParameterOwnership::unsupported, "compiler/linker pipeline", "classifier-policy"},
    SemanticVariantCase{ParameterTool::compiler, "kernel", "/kernel", "pipeline-changing", ParameterOwnership::unsupported, "kernel-mode compilation", "kernel-target-policy"},
    SemanticVariantCase{ParameterTool::compiler, "windows-runtime", "/ZW", "pipeline-changing", ParameterOwnership::unsupported, "target pipeline", "classifier-policy"},
    SemanticVariantCase{ParameterTool::linker, "driver", "/DRIVER", "pipeline-changing", ParameterOwnership::unsupported, "kernel-driver target shape", "kernel-target-policy"},
    SemanticVariantCase{ParameterTool::librarian, "list-mode", "/LIST", "mode-switching", ParameterOwnership::unsupported, "operating mode", "classifier-policy"},
    SemanticVariantCase{ParameterTool::librarian, "extract-mode", "/EXTRACT:member.obj", "mode-switching", ParameterOwnership::unsupported, "archive membership", "classifier-policy"},
    SemanticVariantCase{ParameterTool::linker, "fastlink", "/DEBUG:FASTLINK", "version-dependent", ParameterOwnership::passthrough, "toolchain-version conditional", "MsvcParameterCapabilities"},
    SemanticVariantCase{ParameterTool::compiler, "obsolete-trigraphs", "/Zc:trigraphs", "version-dependent", ParameterOwnership::unsupported, "deprecated, obsolete, or removed", "classifier-policy"},
};

constexpr std::array kRequiredRisks{
    std::string_view{"artifact-producing"},
    std::string_view{"file-input-bearing"},
    std::string_view{"response-file-bearing"},
    std::string_view{"cross-stage"},
    std::string_view{"mode-switching"},
    std::string_view{"version-dependent"},
    std::string_view{"pipeline-changing"},
};

const char* tool_name(const ParameterTool tool) {
    switch (tool) {
    case ParameterTool::compiler: return "compiler";
    case ParameterTool::linker: return "linker";
    case ParameterTool::librarian: return "librarian";
    }
    return "invalid";
}

const char* ownership_name(const ParameterOwnership ownership) {
    switch (ownership) {
    case ParameterOwnership::mqb_owned: return "mqb_owned";
    case ParameterOwnership::semantic: return "semantic";
    case ParameterOwnership::passthrough: return "passthrough";
    case ParameterOwnership::unsupported: return "unsupported";
    }
    return "invalid";
}

bool route_rejects_as_unsupported(const SemanticVariantCase& item) {
    const std::vector<std::string> arguments{std::string{item.variant}};
    switch (item.tool) {
    case ParameterTool::compiler: {
        const auto routed = MsvcParameterEngine::route_compiler(arguments);
        return !routed && routed.error().code == ParameterErrorCode::unsupported_option
            && routed.error().message.find(item.rationale_contains) != std::string::npos;
    }
    case ParameterTool::linker: {
        const auto routed = MsvcParameterEngine::route_linker(arguments);
        return !routed && routed.error().code == ParameterErrorCode::unsupported_option
            && routed.error().message.find(item.rationale_contains) != std::string::npos;
    }
    case ParameterTool::librarian: {
        const auto routed = MsvcParameterEngine::route_librarian(arguments);
        return !routed && routed.error().code == ParameterErrorCode::unsupported_option
            && routed.error().message.find(item.rationale_contains) != std::string::npos;
    }
    }
    return false;
}

} // namespace

int main() {
    int failures = 0;
    for (const auto risk : kRequiredRisks) {
        bool present = false;
        for (const auto& item : kCases) if (item.risk == risk) { present = true; break; }
        if (!present) { ++failures; std::cerr << "MISSING_RISK\t" << risk << '\n'; }
    }
    for (std::size_t left = 0; left < kCases.size(); ++left) {
        for (std::size_t right = left + 1; right < kCases.size(); ++right) {
            if (kCases[left].tool == kCases[right].tool && kCases[left].variant == kCases[right].variant) {
                ++failures;
                std::cerr << "DUPLICATE\t" << tool_name(kCases[left].tool) << '\t' << kCases[left].variant << '\n';
            }
        }
    }
    std::cout << "tool\tfamily\tvariant\trisk\texpected_ownership\tactual_ownership\tbehavioral_gate\trationale\n";
    for (const auto& item : kCases) {
        const auto classified = MsvcParameterEngine::classify(item.tool, item.variant);
        bool ok = classified.ownership == item.expected;
        if (!item.rationale_contains.empty() && classified.rationale.find(item.rationale_contains) == std::string::npos) ok = false;
        if (item.require_route_rejection && !route_rejects_as_unsupported(item)) ok = false;
        std::cout << tool_name(item.tool) << '\t' << item.family << '\t' << item.variant << '\t' << item.risk << '\t'
                  << ownership_name(item.expected) << '\t' << ownership_name(classified.ownership) << '\t'
                  << item.behavioral_gate << '\t' << classified.rationale << '\n';
        if (!ok) {
            ++failures;
            std::cerr << "SEMANTIC_VARIANT\t" << tool_name(item.tool) << '\t' << item.variant
                      << "\texpected=" << ownership_name(item.expected) << "\tactual=" << ownership_name(classified.ownership)
                      << "\trationale=" << classified.rationale << '\n';
        }
    }
    if (failures != 0) { std::cerr << failures << " semantic variant inventory failure(s)\n"; return 1; }
    std::cerr << "Semantic variant inventory: " << kCases.size() << " high-risk concrete variants across "
              << kRequiredRisks.size() << " risk classes\n";
    return 0;
}
