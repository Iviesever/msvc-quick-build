#include <filesystem>
#include <iostream>
#include <string_view>
#include <utility>

#include "mqb/core/Artifact.hpp"
#include "mqb/core/BuildSignature.hpp"
#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/ToolchainIdentity.hpp"
#include "mqb/core/TranslationUnit.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

mqb::TranslationUnit make_unit() {
    mqb::TranslationUnit unit;
    unit.source = std::filesystem::path{"src/main.cpp"};
    unit.kind = mqb::TranslationUnitKind::source;
    unit.dependencies = {std::filesystem::path{"include/app.hpp"}};
    unit.outputs = {
        mqb::Artifact{std::filesystem::path{"build/main.obj"}, mqb::ArtifactKind::object},
    };
    return unit;
}

mqb::ToolchainIdentity make_toolchain() {
    return mqb::ToolchainIdentity{
        .compiler = std::filesystem::path{"toolchain/cl.exe"},
        .version = "19.50.12345",
        .binary_stamp = "size=123456;mtime=987654321",
    };
}

mqb::CompilerOptions make_options() {
    mqb::CompilerOptions options;
    options.configuration = mqb::BuildConfiguration::debug;
    options.architecture = mqb::Architecture::x64;
    options.standard = mqb::CppStandard::cpp23;
    options.defines = {"UNICODE", "MQB_TEST=1"};
    options.include_directories = {
        std::filesystem::path{"include"},
        std::filesystem::path{"vendor/include"},
    };
    options.additional_arguments = {"/EHsc", "/utf-8"};
    return options;
}

} // namespace

int main() {
    const auto unit = make_unit();
    const auto toolchain = make_toolchain();
    const auto options = make_options();

    const auto baseline = mqb::BuildSignature::for_compile(unit, toolchain, options);
    const auto repeated = mqb::BuildSignature::for_compile(unit, toolchain, options);

    expect(baseline == repeated, "identical compile state should produce an identical signature");
    expect(baseline.hex().size() == 32, "signature should expose a stable 128-bit hexadecimal key");

    auto normalized_unit = unit;
    normalized_unit.source = std::filesystem::path{"src/./generated/../main.cpp"};
    expect(mqb::BuildSignature::for_compile(normalized_unit, toolchain, options) == baseline,
           "lexically equivalent source paths should produce the same signature");

    auto release_options = options;
    release_options.configuration = mqb::BuildConfiguration::release;
    expect(mqb::BuildSignature::for_compile(unit, toolchain, release_options) != baseline,
           "changing build configuration should invalidate the compile signature");

    auto x86_options = options;
    x86_options.architecture = mqb::Architecture::x86;
    expect(mqb::BuildSignature::for_compile(unit, toolchain, x86_options) != baseline,
           "changing target architecture should invalidate the compile signature");

    auto define_options = options;
    define_options.defines.emplace_back("FEATURE_X=1");
    expect(mqb::BuildSignature::for_compile(unit, toolchain, define_options) != baseline,
           "changing preprocessor definitions should invalidate the compile signature");

    auto reordered_includes = options;
    std::swap(reordered_includes.include_directories[0], reordered_includes.include_directories[1]);
    expect(mqb::BuildSignature::for_compile(unit, toolchain, reordered_includes) != baseline,
           "include search order should be part of compiler identity");

    auto argument_options = options;
    argument_options.additional_arguments.emplace_back("/volatile:iso");
    expect(mqb::BuildSignature::for_compile(unit, toolchain, argument_options) != baseline,
           "additional compiler arguments should participate in the signature");

    auto changed_toolchain = toolchain;
    changed_toolchain.binary_stamp = "size=123456;mtime=987654322";
    expect(mqb::BuildSignature::for_compile(unit, changed_toolchain, options) != baseline,
           "changing compiler binary identity should invalidate the signature");

    auto changed_source = unit;
    changed_source.source = std::filesystem::path{"src/other.cpp"};
    expect(mqb::BuildSignature::for_compile(changed_source, toolchain, options) != baseline,
           "source identity should participate in the signature");

    auto module_unit = unit;
    module_unit.kind = mqb::TranslationUnitKind::module_interface;
    module_unit.outputs.push_back(mqb::Artifact{
        .path = "build/ifc/main.ifc",
        .kind = mqb::ArtifactKind::module_interface,
    });
    const auto module_baseline = mqb::BuildSignature::for_compile(module_unit, toolchain, options);
    expect(module_baseline != baseline,
           "translation-unit kind and module output state should participate in the signature");

    auto moved_ifc = module_unit;
    moved_ifc.outputs.back().path = "another-ifc/main.ifc";
    expect(mqb::BuildSignature::for_compile(moved_ifc, toolchain, options) != module_baseline,
           "planned compiled-module output path should participate in provider identity");

    auto consumer = unit;
    consumer.module_references = {
        mqb::ModuleReference{.logical_name = "math", .interface_file = "ifc/math.ifc"},
    };
    const auto consumer_baseline = mqb::BuildSignature::for_compile(consumer, toolchain, options);
    expect(consumer_baseline != baseline,
           "module references should participate in consumer recipe identity");

    auto renamed_reference = consumer;
    renamed_reference.module_references[0].logical_name = "math2";
    expect(mqb::BuildSignature::for_compile(renamed_reference, toolchain, options)
               != consumer_baseline,
           "module logical-name mapping should participate in consumer identity");

    auto moved_reference = consumer;
    moved_reference.module_references[0].interface_file = "ifc/other-math.ifc";
    expect(mqb::BuildSignature::for_compile(moved_reference, toolchain, options)
               != consumer_baseline,
           "referenced IFC path should participate in consumer identity");

    auto reordered_references = consumer;
    reordered_references.module_references.push_back(
        mqb::ModuleReference{.logical_name = "stats", .interface_file = "ifc/stats.ifc"});
    const auto two_references = mqb::BuildSignature::for_compile(
        reordered_references, toolchain, options);
    std::swap(reordered_references.module_references[0], reordered_references.module_references[1]);
    expect(mqb::BuildSignature::for_compile(reordered_references, toolchain, options)
               != two_references,
           "ordered module-reference routing should be stable build identity");

    auto header_consumer = unit;
    header_consumer.header_unit_references = {
        mqb::HeaderUnitReference{
            .header_name = "util.hpp",
            .lookup_method = mqb::HeaderUnitLookupMethod::quote,
            .interface_file = "ifc/util.ifc",
        },
    };
    const auto header_baseline = mqb::BuildSignature::for_compile(
        header_consumer, toolchain, options);
    expect(header_baseline != baseline,
           "header-unit references should participate in consumer recipe identity");

    auto angle_header = header_consumer;
    angle_header.header_unit_references[0].lookup_method = mqb::HeaderUnitLookupMethod::angle;
    expect(mqb::BuildSignature::for_compile(angle_header, toolchain, options) != header_baseline,
           "header-unit lookup method should participate in consumer identity");

    auto renamed_header = header_consumer;
    renamed_header.header_unit_references[0].header_name = "other.hpp";
    expect(mqb::BuildSignature::for_compile(renamed_header, toolchain, options) != header_baseline,
           "header-unit import spelling should participate in consumer identity");

    auto moved_header_ifc = header_consumer;
    moved_header_ifc.header_unit_references[0].interface_file = "ifc/other-util.ifc";
    expect(mqb::BuildSignature::for_compile(moved_header_ifc, toolchain, options) != header_baseline,
           "header-unit IFC path should participate in consumer identity");

    auto dependency_only_change = unit;
    dependency_only_change.dependencies.emplace_back("include/transitive.hpp");
    expect(mqb::BuildSignature::for_compile(dependency_only_change, toolchain, options) == baseline,
           "dependency membership belongs to freshness validation, not compile recipe identity");

    auto output_only_change = unit;
    output_only_change.outputs.front().path = std::filesystem::path{"another-cache/main.obj"};
    expect(mqb::BuildSignature::for_compile(output_only_change, toolchain, options) == baseline,
           "ordinary object placement should not change compiler recipe identity");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_build_signature_tests passed\n";
    return 0;
}
