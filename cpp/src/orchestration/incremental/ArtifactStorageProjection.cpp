#include "mqb/orchestration/ArtifactStorageProjection.hpp"

#include <utility>

#include "mqb/orchestration/MsvcModuleTargetCoordinator.hpp"

namespace mqb::orchestration {
namespace {
using Role = ArtifactPathRole;
using Kind = ArtifactStageKind;
void add(ArtifactStorageStage& s, const std::filesystem::path& path, Role role) {
    if (!path.empty()) s.paths.push_back({path, role});
}
void add(ArtifactStorageStage& s, const std::vector<std::filesystem::path>& paths, Role role) {
    for (const auto& p : paths) add(s, p, role);
}
void append(ArtifactStorageReferences& into, ArtifactStorageReferences from) {
    for (auto& s : from.stages) into.stages.push_back(std::move(s));
}
void unit_paths(ArtifactStorageStage& s, const TranslationUnit& unit, const CompilerOptions& options) {
    add(s, unit.source, Role::input);
    add(s, unit.dependencies, Role::input);
    for (const auto& r : unit.module_references) add(s, r.interface_file, Role::input);
    for (const auto& r : unit.header_unit_references) add(s, r.interface_file, Role::input);
    for (const auto& a : unit.outputs) add(s, a.path, Role::declared_output);
    if (options.precompiled_header) {
        add(s, options.precompiled_header->header, Role::input);
        // A producer's declared outputs are authoritative; do not invent a new
        // PCH output merely because a compiler policy mentions a create role.
        if (options.precompiled_header->role == PrecompiledHeaderRole::use)
            add(s, options.precompiled_header->artifact, Role::input);
    }
}
ArtifactStorageStage source_stage(const SourceArtifactAssociation& source, const CompilerOptions& options) {
    ArtifactStorageStage s{.kind=Kind::compile, .completion=source.completion,
                           .source_has_warnings=source.has_warnings,
                           .configuration=options.configuration};
    // This older source record does not carry its own cwd or saved cache state.
    // Neither is reconstructed from the terminal link or a missing warning.
    add(s, source.source, Role::input); add(s, source.object, Role::declared_output);
    add(s, source.dependencies, Role::metadata_reference); add(s, source.compile_cache, Role::metadata_reference);
    if (options.precompiled_header && options.precompiled_header->role == PrecompiledHeaderRole::use) {
        add(s, options.precompiled_header->header, Role::input);
        add(s, options.precompiled_header->artifact, Role::input);
    }
    return s;
}
} // namespace

ArtifactStorageReferences project_storage_references(const LinkArtifactRecord& r) {
    ArtifactStorageStage s{.kind=Kind::link, .completion=r.completion, .cache_state=r.cache_state,
        .configuration=r.options.configuration, .working_directory=r.working_directory};
    add(s, r.association.objects, Role::input); add(s, r.association.libraries, Role::input);
    add(s, r.association.file_inputs, Role::input); add(s, r.association.output, Role::declared_output);
    add(s, r.association.side_outputs, Role::declared_output); add(s, r.cache_file, Role::metadata_reference);
    return {.stages={std::move(s)}};
}
ArtifactStorageReferences project_storage_references(const ArchiveArtifactRecord& r) {
    ArtifactStorageStage s{.kind=Kind::archive, .completion=r.completion, .cache_state=r.cache_state,
        .working_directory=r.working_directory};
    // LIB's recipe does not reveal a compiler Debug/Release configuration.
    add(s, r.association.objects, Role::input); add(s, r.association.output, Role::declared_output);
    add(s, r.cache_file, Role::metadata_reference);
    return {.stages={std::move(s)}};
}
ArtifactStorageReferences project_storage_references(const TargetArtifactRecord& r) {
    ArtifactStorageReferences out{.caller_label=r.caller_label};
    for (const auto& source : r.sources) out.stages.push_back(source_stage(source, r.compiler_options));
    append(out, project_storage_references(r.link));
    add(out.stages.back(), r.additional_object_inputs, Role::input);
    return out;
}
ArtifactStorageReferences project_storage_references(const StaticTargetArtifactRecord& r) {
    ArtifactStorageReferences out{.caller_label=r.caller_label};
    for (const auto& source : r.sources) out.stages.push_back(source_stage(source, r.compiler_options));
    append(out, project_storage_references(r.archive));
    add(out.stages.back(), r.additional_object_inputs, Role::input);
    return out;
}
ArtifactStorageReferences project_storage_references(const PchArtifactRecord& r) {
    ArtifactStorageStage s{.kind=Kind::pch, .completion=r.completion, .cache_state=r.cache_state,
        .configuration=r.compiler_options.configuration, .working_directory=r.working_directory};
    add(s, r.input_header, Role::input);
    unit_paths(s, r.creator, r.compiler_options);
    // Creator is MQB materialized state and also the compiler input. Preserve
    // both roles; declared_output is not a claim of a new write on cache reuse.
    add(s, r.creator.source, Role::declared_output);
    add(s, r.dependencies, Role::metadata_reference); add(s, r.compile_cache, Role::metadata_reference);
    return {.caller_label=r.caller_label, .stages={std::move(s)}};
}
ArtifactStorageReferences project_storage_references(const ModuleCompileArtifactRecord& r) {
    ArtifactStorageStage s{.kind=Kind::compile, .completion=r.completion, .cache_state=r.cache_state,
        .configuration=r.compiler_options.configuration, .working_directory=r.working_directory};
    unit_paths(s, r.unit, r.compiler_options);
    add(s, r.dependencies, Role::metadata_reference); add(s, r.compile_cache, Role::metadata_reference);
    if (r.module_scan_output) add(s, *r.module_scan_output, Role::metadata_reference);
    return {.stages={std::move(s)}};
}
ArtifactStorageReferences project_storage_references(const ModuleScanArtifactRecord& r) {
    const auto& invocation = r.recipe.invocation;
    ArtifactStorageStage s{.kind=Kind::scan, .completion=r.completion,
        .configuration=invocation.options.configuration, .working_directory=invocation.working_directory};
    add(s, invocation.source, Role::input); add(s, invocation.output_file, Role::metadata_reference);
    add(s, r.compile_cache_reference, Role::metadata_reference);
    // Do not turn P1689 output declarations into already produced artifacts or
    // copy ProcessSpec.environment into this deliberately limited projection.
    return {.caller_label=r.caller_label, .stages={std::move(s)}};
}
ArtifactStorageReferences project_storage_references(const ModuleCompileWaveArtifactRecord& r) {
    ArtifactStorageReferences out{.caller_label=r.caller_label};
    for (const auto& node : r.compiles) append(out, project_storage_references(node));
    for (const auto& node : r.header_unit_compiles) append(out, project_storage_references(node));
    return out;
}
ArtifactStorageReferences project_storage_references(const ModuleTargetArtifactRecord& r) {
    ArtifactStorageReferences out{.caller_label=r.caller_label};
    for (const auto& scan : r.scans) {
        auto projected = project_storage_references(scan.scan);
        projected.stages[0].toolchain_source = scan.toolchain_owned;
        append(out, std::move(projected));
    }
    append(out, project_storage_references(r.compiles));
    append(out, project_storage_references(r.link));
    return out;
}
} // namespace mqb::orchestration
