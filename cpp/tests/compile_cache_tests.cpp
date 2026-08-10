#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

#include "mqb/core/Artifact.hpp"
#include "mqb/core/BuildSignature.hpp"
#include "mqb/core/BuildTypes.hpp"
#include "mqb/core/CompileCache.hpp"
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

[[nodiscard]] bool has_reason(
    const mqb::CompileCacheValidation& validation,
    const mqb::BuildReason reason) {
    return std::find(validation.reasons.begin(), validation.reasons.end(), reason)
        != validation.reasons.end();
}

[[nodiscard]] std::filesystem::file_time_type time_at(const int seconds) {
    return std::filesystem::file_time_type{} + std::chrono::seconds{seconds};
}

mqb::TranslationUnit make_unit() {
    mqb::TranslationUnit unit;
    unit.source = "src/main.cpp";
    unit.kind = mqb::TranslationUnitKind::source;
    unit.outputs = {
        mqb::Artifact{std::filesystem::path{"build/main.obj"}, mqb::ArtifactKind::object},
    };
    return unit;
}

mqb::ToolchainIdentity make_toolchain() {
    return mqb::ToolchainIdentity{
        .compiler = "toolchain/cl.exe",
        .version = "19.50.12345",
        .binary_stamp = "stamp-a",
    };
}

mqb::CompilerOptions make_options() {
    mqb::CompilerOptions options;
    options.configuration = mqb::BuildConfiguration::debug;
    options.architecture = mqb::Architecture::x64;
    options.standard = mqb::CppStandard::cpp23;
    options.defines = {"UNICODE"};
    options.include_directories = {"include"};
    options.additional_arguments = {"/EHsc"};
    return options;
}

mqb::CompileCacheEntry make_entry(
    const mqb::TranslationUnit& unit,
    const mqb::ToolchainIdentity& toolchain,
    const mqb::CompilerOptions& options) {
    const auto object = std::find_if(
        unit.outputs.begin(),
        unit.outputs.end(),
        [](const mqb::Artifact& artifact) {
            return artifact.kind == mqb::ArtifactKind::object;
        });
    return mqb::CompileCacheEntry{
        .source = unit.source,
        .kind = unit.kind,
        .toolchain = toolchain,
        .signature = mqb::BuildSignature::for_compile(unit, toolchain, options),
        .object = object == unit.outputs.end() ? mqb::Artifact{} : *object,
        .dependencies = {
            std::filesystem::path{"include/app.hpp"},
            std::filesystem::path{"include/config.hpp"},
        },
    };
}

} // namespace

int main() {
    const auto unit = make_unit();
    const auto toolchain = make_toolchain();
    const auto options = make_options();
    const auto entry = make_entry(unit, toolchain, options);

    const mqb::FileSnapshot source{
        .path = unit.source,
        .exists = true,
        .modified = time_at(10),
    };
    const mqb::FileSnapshot object{
        .path = entry.object.path,
        .exists = true,
        .modified = time_at(30),
    };
    const std::vector<mqb::FileSnapshot> outputs{object};
    const std::vector<mqb::FileSnapshot> dependencies{
        mqb::FileSnapshot{.path = "include/app.hpp", .exists = true, .modified = time_at(20)},
        mqb::FileSnapshot{.path = "include/config.hpp", .exists = true, .modified = time_at(25)},
    };

    const auto fresh = mqb::CompileCacheValidator::validate(
        unit, toolchain, options, entry, source, outputs, dependencies);
    expect(fresh.reusable(), "unchanged recipe and older dependencies should reuse the object");
    expect(fresh.reasons.empty(), "reusable cache entry should not invent rebuild reasons");

    const auto no_entry = mqb::CompileCacheValidator::validate(
        unit, toolchain, options, std::nullopt, source, outputs, dependencies);
    expect(!no_entry.reusable(), "missing cache metadata should force a rebuild");
    expect(has_reason(no_entry, mqb::BuildReason::missing_cache_entry),
           "missing cache metadata should be explainable");

    auto missing_outputs = outputs;
    missing_outputs[0].exists = false;
    const auto missing_output = mqb::CompileCacheValidator::validate(
        unit, toolchain, options, entry, source, missing_outputs, dependencies);
    expect(has_reason(missing_output, mqb::BuildReason::missing_output),
           "missing object artifact should force a rebuild");

    auto newer_source = source;
    newer_source.modified = time_at(40);
    const auto source_changed = mqb::CompileCacheValidator::validate(
        unit, toolchain, options, entry, newer_source, outputs, dependencies);
    expect(has_reason(source_changed, mqb::BuildReason::source_changed),
           "source newer than object should invalidate the cache");

    auto newer_dependencies = dependencies;
    newer_dependencies[0].modified = time_at(50);
    newer_dependencies[1].modified = time_at(60);
    const auto dependency_changed = mqb::CompileCacheValidator::validate(
        unit, toolchain, options, entry, source, outputs, newer_dependencies);
    expect(has_reason(dependency_changed, mqb::BuildReason::dependency_changed),
           "newer cached dependency should invalidate the object");
    expect(std::count(
               dependency_changed.reasons.begin(),
               dependency_changed.reasons.end(),
               mqb::BuildReason::dependency_changed) == 1,
           "multiple stale dependencies should collapse to one rebuild reason");

    auto missing_dependency = dependencies;
    missing_dependency[0].exists = false;
    const auto dependency_missing = mqb::CompileCacheValidator::validate(
        unit, toolchain, options, entry, source, outputs, missing_dependency);
    expect(has_reason(dependency_missing, mqb::BuildReason::dependency_changed),
           "deleted cached dependency should invalidate the object");

    std::vector<mqb::FileSnapshot> incomplete_dependencies{dependencies[0]};
    const auto dependency_snapshot_missing = mqb::CompileCacheValidator::validate(
        unit, toolchain, options, entry, source, outputs, incomplete_dependencies);
    expect(has_reason(dependency_snapshot_missing, mqb::BuildReason::dependency_changed),
           "missing dependency snapshot should conservatively rebuild");

    auto changed_toolchain = toolchain;
    changed_toolchain.binary_stamp = "stamp-b";
    const auto toolchain_changed = mqb::CompileCacheValidator::validate(
        unit, changed_toolchain, options, entry, source, outputs, dependencies);
    expect(has_reason(toolchain_changed, mqb::BuildReason::toolchain_changed),
           "compiler binary identity change should invalidate the cache");
    expect(!has_reason(toolchain_changed, mqb::BuildReason::compiler_options_changed),
           "toolchain-only change should not be mislabeled as compiler options");

    auto release_options = options;
    release_options.configuration = mqb::BuildConfiguration::release;
    const auto options_changed = mqb::CompileCacheValidator::validate(
        unit, toolchain, release_options, entry, source, outputs, dependencies);
    expect(has_reason(options_changed, mqb::BuildReason::compiler_options_changed),
           "compile recipe change should invalidate the cache");

    auto moved_source = unit;
    moved_source.source = "src/other.cpp";
    const mqb::FileSnapshot moved_source_snapshot{
        .path = moved_source.source,
        .exists = true,
        .modified = time_at(10),
    };
    const auto source_identity_changed = mqb::CompileCacheValidator::validate(
        moved_source,
        toolchain,
        options,
        entry,
        moved_source_snapshot,
        outputs,
        dependencies);
    expect(has_reason(source_identity_changed, mqb::BuildReason::source_changed),
           "translation-unit identity change should invalidate the cache");
    expect(!has_reason(source_identity_changed, mqb::BuildReason::compiler_options_changed),
           "source identity change should not be mislabeled as compiler options");

    {
        auto moved_object_unit = unit;
        moved_object_unit.outputs[0].path = "another/main.obj";
        const std::vector<mqb::FileSnapshot> moved_object_outputs{
            mqb::FileSnapshot{
                .path = "another/main.obj",
                .exists = true,
                .modified = time_at(30),
            },
        };
        const auto moved_object = mqb::CompileCacheValidator::validate(
            moved_object_unit,
            toolchain,
            options,
            entry,
            source,
            moved_object_outputs,
            dependencies);
        expect(has_reason(moved_object, mqb::BuildReason::missing_output),
               "cache metadata for a different object path must not authorize reuse");
    }

    {
        auto module_unit = unit;
        module_unit.kind = mqb::TranslationUnitKind::module_interface;
        module_unit.source = "src/math.ixx";
        module_unit.outputs = {
            mqb::Artifact{.path = "build/math.obj", .kind = mqb::ArtifactKind::object},
            mqb::Artifact{.path = "ifc/math.ifc", .kind = mqb::ArtifactKind::module_interface},
        };
        const auto module_entry = make_entry(module_unit, toolchain, options);
        const mqb::FileSnapshot module_source{
            .path = module_unit.source,
            .exists = true,
            .modified = time_at(10),
        };
        const std::vector<mqb::FileSnapshot> module_outputs{
            mqb::FileSnapshot{.path = "build/math.obj", .exists = true, .modified = time_at(30)},
            mqb::FileSnapshot{.path = "ifc/math.ifc", .exists = true, .modified = time_at(30)},
        };
        const auto module_fresh = mqb::CompileCacheValidator::validate(
            module_unit,
            toolchain,
            options,
            module_entry,
            module_source,
            module_outputs,
            dependencies);
        expect(module_fresh.reusable(),
               "module provider should reuse cache only when both object and IFC exist");

        auto missing_ifc_outputs = module_outputs;
        missing_ifc_outputs[1].exists = false;
        const auto missing_ifc = mqb::CompileCacheValidator::validate(
            module_unit,
            toolchain,
            options,
            module_entry,
            module_source,
            missing_ifc_outputs,
            dependencies);
        expect(has_reason(missing_ifc, mqb::BuildReason::missing_output),
               "missing IFC output must invalidate an otherwise warm provider cache");

        auto moved_ifc_unit = module_unit;
        moved_ifc_unit.outputs[1].path = "ifc-v2/math.ifc";
        const std::vector<mqb::FileSnapshot> moved_ifc_outputs{
            module_outputs[0],
            mqb::FileSnapshot{.path = "ifc-v2/math.ifc", .exists = true, .modified = time_at(30)},
        };
        const auto moved_ifc = mqb::CompileCacheValidator::validate(
            moved_ifc_unit,
            toolchain,
            options,
            module_entry,
            module_source,
            moved_ifc_outputs,
            dependencies);
        expect(has_reason(moved_ifc, mqb::BuildReason::compiler_options_changed),
               "planned IFC routing change must invalidate provider signature identity");
    }

    {
        auto consumer = unit;
        consumer.module_references = {
            mqb::ModuleReference{.logical_name = "math", .interface_file = "ifc/math.ifc"},
        };
        auto consumer_entry = make_entry(consumer, toolchain, options);
        consumer_entry.dependencies.push_back("ifc/math.ifc");
        const std::vector<mqb::FileSnapshot> consumer_dependencies{
            dependencies[0],
            dependencies[1],
            mqb::FileSnapshot{.path = "ifc/math.ifc", .exists = true, .modified = time_at(20)},
        };
        const auto consumer_fresh = mqb::CompileCacheValidator::validate(
            consumer,
            toolchain,
            options,
            consumer_entry,
            source,
            outputs,
            consumer_dependencies);
        expect(consumer_fresh.reusable(),
               "consumer may reuse when referenced IFC is older than its object");

        auto newer_ifc = consumer_dependencies;
        newer_ifc.back().modified = time_at(40);
        const auto imported_module_changed = mqb::CompileCacheValidator::validate(
            consumer,
            toolchain,
            options,
            consumer_entry,
            source,
            outputs,
            newer_ifc);
        expect(has_reason(imported_module_changed, mqb::BuildReason::dependency_changed),
               "newer imported IFC should invalidate the consumer object");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_compile_cache_tests passed\n";
    return 0;
}
