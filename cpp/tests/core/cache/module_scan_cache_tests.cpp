#include <chrono>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

#include "mqb/core/BuildSignature.hpp"
#include "mqb/core/CompileCache.hpp"
#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/FileSnapshot.hpp"
#include "mqb/core/ToolchainIdentity.hpp"
#include "mqb/core/TranslationUnit.hpp"

namespace {

namespace fs = std::filesystem;
int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

[[nodiscard]] fs::file_time_type time_at(const int seconds) {
    return fs::file_time_type{} + std::chrono::seconds{seconds};
}

[[nodiscard]] mqb::FileSnapshot snapshot(
    fs::path path,
    const int seconds,
    const bool exists = true) {
    return mqb::FileSnapshot{
        .path = std::move(path),
        .exists = exists,
        .modified = time_at(seconds),
    };
}

[[nodiscard]] mqb::ToolchainIdentity make_toolchain() {
    return mqb::ToolchainIdentity{
        .compiler = "toolchain/cl.exe",
        .version = "19.51.10000",
        .binary_stamp = "size=123;mtime=456",
    };
}

[[nodiscard]] mqb::CompilerOptions make_options() {
    mqb::CompilerOptions options;
    options.configuration = mqb::BuildConfiguration::debug;
    options.architecture = mqb::Architecture::x64;
    options.standard = mqb::CppStandard::latest;
    options.defines = {"FEATURE=1"};
    options.include_directories = {"include", "vendor/include"};
    options.additional_arguments = {"/experimental:module"};
    return options;
}

} // namespace

int main() {
    const fs::path source{"src/main.cpp"};
    const auto toolchain = make_toolchain();
    const auto options = make_options();

    const auto baseline = mqb::BuildSignature::for_module_scan(
        source,
        mqb::TranslationUnitKind::source,
        toolchain,
        options);
    expect(
        baseline == mqb::BuildSignature::for_module_scan(
            source,
            mqb::TranslationUnitKind::source,
            toolchain,
            options),
        "identical module-scan recipes should produce identical signatures");

    expect(
        baseline != mqb::BuildSignature::for_module_scan(
            "src/other.cpp",
            mqb::TranslationUnitKind::source,
            toolchain,
            options),
        "source identity should participate in module-scan signature");
    expect(
        baseline != mqb::BuildSignature::for_module_scan(
            source,
            mqb::TranslationUnitKind::module_interface,
            toolchain,
            options),
        "translation-unit kind should participate in module-scan signature");

    auto changed_toolchain = toolchain;
    changed_toolchain.binary_stamp = "size=123;mtime=457";
    expect(
        baseline != mqb::BuildSignature::for_module_scan(
            source,
            mqb::TranslationUnitKind::source,
            changed_toolchain,
            options),
        "compiler binary identity should participate in module-scan signature");

    auto release = options;
    release.configuration = mqb::BuildConfiguration::release;
    expect(
        baseline != mqb::BuildSignature::for_module_scan(
            source,
            mqb::TranslationUnitKind::source,
            toolchain,
            release),
        "configuration macro policy should participate in module-scan signature");

    auto x86 = options;
    x86.architecture = mqb::Architecture::x86;
    expect(
        baseline != mqb::BuildSignature::for_module_scan(
            source,
            mqb::TranslationUnitKind::source,
            toolchain,
            x86),
        "target architecture should participate in module-scan signature");

    auto standard = options;
    standard.standard = mqb::CppStandard::cpp20;
    expect(
        baseline != mqb::BuildSignature::for_module_scan(
            source,
            mqb::TranslationUnitKind::source,
            toolchain,
            standard),
        "language mode should participate in module-scan signature");

    auto define = options;
    define.defines.emplace_back("EXTRA=1");
    expect(
        baseline != mqb::BuildSignature::for_module_scan(
            source,
            mqb::TranslationUnitKind::source,
            toolchain,
            define),
        "preprocessor definitions should participate in module-scan signature");

    auto includes = options;
    std::swap(includes.include_directories[0], includes.include_directories[1]);
    expect(
        baseline != mqb::BuildSignature::for_module_scan(
            source,
            mqb::TranslationUnitKind::source,
            toolchain,
            includes),
        "include search order should participate in module-scan signature");

    auto arguments = options;
    arguments.additional_arguments.emplace_back("/Zc:preprocessor-");
    expect(
        baseline != mqb::BuildSignature::for_module_scan(
            source,
            mqb::TranslationUnitKind::source,
            toolchain,
            arguments),
        "raw scan-visible compiler arguments should participate in module-scan signature");

    auto runtime = options;
    runtime.runtime_library = mqb::RuntimeLibrary::mt;
    expect(
        baseline == mqb::BuildSignature::for_module_scan(
            source,
            mqb::TranslationUnitKind::source,
            toolchain,
            runtime),
        "runtime library policy should not invalidate P1689 topology identity");

    auto ltcg = options;
    ltcg.link_time_code_generation = true;
    expect(
        baseline == mqb::BuildSignature::for_module_scan(
            source,
            mqb::TranslationUnitKind::source,
            toolchain,
            ltcg),
        "LTCG policy should not invalidate P1689 topology identity");

    const mqb::ModuleScanEvidence evidence{
        .signature = baseline,
        .source = snapshot(source, 10),
        .output = snapshot(".mqb/scan/main.json", 20),
        .dependencies = {
            snapshot("include/app.hpp", 11),
            snapshot("include/config.hpp", 12),
        },
    };
    const std::vector<mqb::FileSnapshot> dependencies = evidence.dependencies;

    expect(
        mqb::ModuleScanEvidenceValidator::reusable(
            evidence,
            baseline,
            evidence.source,
            evidence.output,
            dependencies),
        "exact sealed scan evidence should be reusable");

    auto newer_source = evidence.source;
    newer_source.modified = time_at(21);
    expect(
        !mqb::ModuleScanEvidenceValidator::reusable(
            evidence,
            baseline,
            newer_source,
            evidence.output,
            dependencies),
        "source snapshot change should invalidate sealed scan evidence");

    auto changed_output = evidence.output;
    changed_output.modified = time_at(22);
    expect(
        !mqb::ModuleScanEvidenceValidator::reusable(
            evidence,
            baseline,
            evidence.source,
            changed_output,
            dependencies),
        "P1689 artifact replacement should invalidate sealed scan evidence");

    auto changed_dependencies = dependencies;
    changed_dependencies[0].modified = time_at(30);
    expect(
        !mqb::ModuleScanEvidenceValidator::reusable(
            evidence,
            baseline,
            evidence.source,
            evidence.output,
            changed_dependencies),
        "textual dependency mutation should invalidate sealed scan evidence");

    auto missing_dependency = dependencies;
    missing_dependency[1].exists = false;
    expect(
        !mqb::ModuleScanEvidenceValidator::reusable(
            evidence,
            baseline,
            evidence.source,
            evidence.output,
            missing_dependency),
        "deleted textual dependency should invalidate sealed scan evidence");

    expect(
        !mqb::ModuleScanEvidenceValidator::reusable(
            evidence,
            mqb::BuildSignature::for_module_scan(
                source,
                mqb::TranslationUnitKind::source,
                changed_toolchain,
                options),
            evidence.source,
            evidence.output,
            dependencies),
        "scan recipe change should invalidate sealed scan evidence");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_module_scan_cache_tests passed\n";
    return 0;
}
