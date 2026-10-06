#include "mqb/orchestration/ArtifactStorageProjection.hpp"

#include <utility>
#include <algorithm>
#include <set>
#include <type_traits>

#include "mqb/orchestration/MsvcIncrementalStaticTargetCoordinator.hpp"

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

// BEGIN recorded ordinary-target storage projection
namespace mqb::orchestration {
namespace {
using ProjectionIssue = RecordedStorageProjectionIssue;
struct ProjectionRefusal { RecordedStorageProjectionError error; };
struct ProjectionBudget {
    RecordedStorageProjectionLimits left;
    std::optional<std::size_t> source_index;
    explicit ProjectionBudget(RecordedStorageProjectionLimits requested) : left{
        (std::min)(requested.sources, std::size_t{100000}),
        (std::min)(requested.items, std::size_t{1000000}),
        (std::min)(requested.text_code_units, std::size_t{64 * 1024 * 1024})} {}
    [[noreturn]] void fail(ProjectionIssue issue, const char* message) const {
        throw ProjectionRefusal{{issue, source_index, message}};
    }
    void check(bool valid, ProjectionIssue issue, const char* message) const {
        if (!valid) fail(issue, message);
    }
    void count(std::size_t n) {
        check(n <= left.items, ProjectionIssue::limit_exceeded, "selected item limit exceeded");
        left.items -= n;
    }
    void text(std::size_t n) {
        check(n <= left.text_code_units, ProjectionIssue::limit_exceeded, "selected text limit exceeded");
        left.text_code_units -= n;
    }
    void path(const std::filesystem::path& p) { count(1); text(p.native().size()); }
    void string(const std::string& s) { count(1); text(s.size()); }
    void paths(const std::vector<std::filesystem::path>& paths) {
        check(paths.size() <= left.items, ProjectionIssue::limit_exceeded, "path vector limit exceeded");
        for (const auto& p : paths) path(p);
    }
    void identity(const ToolchainIdentity& t) { path(t.compiler); string(t.version); string(t.binary_stamp); }
    void options(const CompilerOptions& o) {
        count(o.defines.size()); for (const auto& s : o.defines) text(s.size());
        paths(o.include_directories);
        count(o.additional_arguments.size()); for (const auto& s : o.additional_arguments) text(s.size());
        count(o.external_module_providers.size());
        for (const auto& p : o.external_module_providers) { string(p.logical_name); path(p.interface_file); }
        if (o.precompiled_header) { path(o.precompiled_header->header); path(o.precompiled_header->artifact); }
    }
};
bool identical_options(const CompilerOptions& a, const CompilerOptions& b) {
    if (a.configuration != b.configuration || a.architecture != b.architecture || a.standard != b.standard ||
        a.runtime_library != b.runtime_library || a.link_time_code_generation != b.link_time_code_generation ||
        a.defines != b.defines || a.include_directories != b.include_directories ||
        a.additional_arguments != b.additional_arguments ||
        a.precompiled_header.has_value() != b.precompiled_header.has_value() ||
        a.external_module_providers.size() != b.external_module_providers.size()) return false;
    if (a.precompiled_header && (a.precompiled_header->header != b.precompiled_header->header ||
        a.precompiled_header->artifact != b.precompiled_header->artifact ||
        a.precompiled_header->role != b.precompiled_header->role)) return false;
    for (std::size_t i = 0; i < a.external_module_providers.size(); ++i) {
        if (a.external_module_providers[i].logical_name != b.external_module_providers[i].logical_name ||
            a.external_module_providers[i].interface_file != b.external_module_providers[i].interface_file) return false;
    }
    return true;
}
bool identical_toolchain(const ToolchainIdentity& a, const ToolchainIdentity& b) {
    return a.compiler == b.compiler && a.version == b.version && a.binary_stamp == b.binary_stamp;
}
// This is a lexical eligibility check only. Casing/alias keys belong to the
// supplied platform authority; there are no canonical/current-path probes.
bool projection_lexical_path(const std::filesystem::path& p) {
    using Char = std::filesystem::path::value_type;
    if (p.empty() || p.native().find(Char{}) != std::filesystem::path::string_type::npos) return false;
    for (const auto& component : p) if (component == "..") return false;
    return true;
}
struct ProjectionPaths {
    ProjectionBudget& budget;
    const StoragePathKey& key;
    std::set<std::string> inputs;
    std::set<std::string> writes;
    std::set<std::string> sources;
    std::optional<std::string> resolve(const std::filesystem::path& p,
                                      const std::optional<std::filesystem::path>& cwd) {
        if (!projection_lexical_path(p)) return std::nullopt;
        auto resolved = p;
        if (!p.is_absolute()) {
            if (p.has_root_path() || !cwd || !cwd->is_absolute() || !projection_lexical_path(*cwd))
                return std::nullopt;
            // Bound temporary concatenation as well as the resulting key.
            budget.text(cwd->native().size());
            resolved = *cwd / p;
        }
        auto value = key(resolved.lexically_normal());
        budget.text(value.size());
        budget.check(!value.empty(), ProjectionIssue::invalid_path, "empty platform path key");
        return value;
    }
    void add_path(ArtifactStorageStage& stage, const std::filesystem::path& p,
                  Role role, bool required = false, bool source = false) {
        budget.path(p);
        auto resolved = resolve(p, stage.working_directory);
        budget.check(!required || resolved.has_value(), ProjectionIssue::invalid_path,
                     "critical path needs its own unambiguous recorded context");
        if (resolved) {
            if (source) budget.check(sources.insert(*resolved).second, ProjectionIssue::path_conflict,
                                     "duplicate source path");
            if (role == Role::input) inputs.insert(*resolved);
            else budget.check(writes.insert(*resolved).second, ProjectionIssue::path_conflict,
                              "conflicting output or metadata path");
        }
        // Preserve every supplied reference, including unresolved input paths.
        // The existing storage join will report unresolved, never infer absence.
        stage.paths.push_back({p, role});
    }
    void finish() {
        for (const auto& p : writes)
            budget.check(!inputs.contains(p), ProjectionIssue::path_conflict,
                         "output or metadata aliases a protected input");
    }
};
ArtifactCacheState checked_cache_state(const CompileCacheEvidence& e,
                                       const IncrementalCompileResult& result, ProjectionBudget& budget) {
    std::size_t save_warnings = 0;
    budget.count(result.warnings.size());
    for (const auto& w : result.warnings) {
        budget.path(w.path); budget.string(w.message);
        if (w.code == IncrementalCompileWarningCode::cache_save_failed) {
            ++save_warnings;
            budget.check(e.save_error && w.path == e.save_error->file && w.message == e.save_error->message,
                ProjectionIssue::outcome_mismatch, "save diagnostic differs from captured failure");
        }
    }
    if (e.save_error) { budget.path(e.save_error->file); budget.string(e.save_error->message); }
    switch (e.state) {
    case CompileCacheEvidenceState::reused:
        budget.check(!result.compiled && !e.request.force_rebuild && !e.save_error && save_warnings == 0,
            ProjectionIssue::outcome_mismatch, "reuse cannot claim compilation, force or save failure");
        return ArtifactCacheState::reused;
    case CompileCacheEvidenceState::saved:
        budget.check(result.compiled && !e.save_error && save_warnings == 0,
            ProjectionIssue::outcome_mismatch, "saved cache requires executed completion without save failure");
        return ArtifactCacheState::saved;
    case CompileCacheEvidenceState::save_failed:
        budget.check(result.compiled && e.save_error && save_warnings == 1 &&
            e.save_error->file == e.request.cache_file,
            ProjectionIssue::outcome_mismatch, "save failure must retain its exact diagnostic");
        return ArtifactCacheState::save_failed;
    }
    budget.fail(ProjectionIssue::outcome_mismatch, "unknown cache evidence outcome");
}
void check_terminal_state(ArtifactCompletion completion, ArtifactCacheState cache, bool executed,
                          ProjectionBudget& budget) {
    budget.check(completion == (executed ? ArtifactCompletion::executed : ArtifactCompletion::reused) &&
        (executed ? (cache == ArtifactCacheState::saved || cache == ArtifactCacheState::save_failed)
                  : cache == ArtifactCacheState::reused),
        ProjectionIssue::terminal_mismatch, "terminal result and record outcomes disagree");
}
template<class Recorded, class Terminal>
std::expected<RecordedTargetStorageReferences, RecordedStorageProjectionError>
project_recorded_target(const Recorded& recorded, const Terminal& terminal, bool terminal_executed,
                        const StoragePathKey& key, RecordedStorageProjectionLimits limits) {
    ProjectionBudget budget{limits};
    try {
        budget.check(bool(key), ProjectionIssue::invalid_path, "a pure platform path key is required");
        const auto& record = recorded.record;
        const auto& values = recorded.cache_evidence.compiles;
        const auto n = record.sources.size();
        budget.check(n <= budget.left.sources, ProjectionIssue::limit_exceeded, "source limit exceeded");
        budget.check(n != 0 && values.size() == n && recorded.result.compiles.size() == n,
            ProjectionIssue::source_count, "source, result and evidence counts must match");
        budget.count(n); budget.options(record.compiler_options);
        if (record.caller_label) { budget.string(record.caller_label->target); budget.string(record.caller_label->generation); }
        RecordedTargetStorageReferences out;
        out.references.caller_label = record.caller_label;
        out.compiles.reserve(n); out.references.stages.reserve(n + 1);
        ProjectionPaths paths{budget, key, {}, {}, {}};
        bool any_compiled = false;
        std::vector<std::string> object_keys; object_keys.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            budget.source_index = i;
            const auto& source = record.sources[i];
            const auto& result = recorded.result.compiles[i];
            const auto& e = values[i]; const auto& request = e.request; const auto& cache = e.cache_entry;
            budget.options(request.options);
            budget.path(source.source); budget.path(source.object); budget.path(source.dependencies); budget.path(source.compile_cache);
            budget.path(result.source); budget.path(request.unit.source); budget.path(cache.source);
            budget.path(request.cache_file); budget.path(request.source_dependencies_file);
            if (request.working_directory) budget.path(*request.working_directory);
            budget.check(source.source == result.source && source.source == request.unit.source &&
                source.dependencies == request.source_dependencies_file && source.compile_cache == request.cache_file,
                ProjectionIssue::source_mismatch, "source order or request association differs");
            budget.check(identical_options(record.compiler_options, request.options),
                ProjectionIssue::options_mismatch, "captured compiler options differ from target record");
            budget.check(request.unit.outputs.size() == 1 && cache.outputs.size() == 1,
                ProjectionIssue::cache_mismatch, "ordinary compile must declare one object");
            budget.path(request.unit.outputs[0].path); budget.path(cache.outputs[0].path);
            // This overload consumes the ordinary target factory, not a module,
            // HU or creator record disguised as a one-object source.
            budget.check(request.unit.kind == TranslationUnitKind::source && !request.unit.header_unit &&
                request.unit.dependencies.empty() && request.unit.module_references.empty() &&
                request.unit.header_unit_references.empty() && !request.module_scan_output && !cache.module_scan &&
                request.unit.outputs.size() == 1 && cache.outputs.size() == 1 &&
                request.unit.outputs[0].kind == ArtifactKind::object && cache.outputs[0].kind == ArtifactKind::object &&
                source.object == request.unit.outputs[0].path && source.object == cache.outputs[0].path &&
                source.source == cache.source && cache.kind == request.unit.kind,
                ProjectionIssue::cache_mismatch, "ordinary request and captured cache shape or paths differ");
            budget.check(source.completion == (result.result.compiled ? ArtifactCompletion::executed : ArtifactCompletion::reused) &&
                source.has_warnings == !result.result.warnings.empty(),
                ProjectionIssue::outcome_mismatch, "source completion or warning flag differs");
            const auto state = checked_cache_state(e, result.result, budget);
            any_compiled |= result.result.compiled;
            budget.identity(e.inspection_toolchain.identity); budget.identity(cache.toolchain);
            ArtifactStorageStage stage{.kind=Kind::compile, .completion=source.completion,
                .cache_state=state, .source_has_warnings=source.has_warnings,
                .configuration=request.options.configuration, .working_directory=request.working_directory};
            paths.add_path(stage, source.source, Role::input, true, true);
            // Recorded dependencies and namespace roots are inputs, not owned
            // compiler outputs. Their original order/spelling stays untouched.
            budget.check(cache.dependencies.size() <= budget.left.items && cache.include_search_roots.size() <= budget.left.items,
                ProjectionIssue::limit_exceeded, "captured dependency vector limit exceeded");
            for (const auto& p : cache.dependencies) paths.add_path(stage, p, Role::input);
            for (const auto& p : cache.include_search_roots) paths.add_path(stage, p, Role::input);
            if (request.options.precompiled_header) {
                const auto& pch = *request.options.precompiled_header;
                budget.check(pch.role == PrecompiledHeaderRole::use, ProjectionIssue::cache_mismatch,
                             "ordinary source is not a PCH creator");
                paths.add_path(stage, pch.header, Role::input);
                paths.add_path(stage, pch.artifact, Role::input);
            }
            paths.add_path(stage, source.object, Role::declared_output, true);
            object_keys.push_back(*paths.resolve(source.object, stage.working_directory));
            paths.add_path(stage, source.dependencies, Role::metadata_reference, true);
            paths.add_path(stage, source.compile_cache, Role::metadata_reference, true);
            out.references.stages.push_back(std::move(stage));
            out.compiles.push_back({e.inspection_toolchain.identity, cache.toolchain, cache.signature,
                !identical_toolchain(e.inspection_toolchain.identity, cache.toolchain),
                request.force_rebuild, e.save_error, result.result.warnings});
        }
        budget.source_index.reset();
        budget.check(any_compiled == recorded.result.any_compiled, ProjectionIssue::outcome_mismatch,
                     "target any_compiled differs from final wave");
        check_terminal_state(terminal.completion, terminal.cache_state, terminal_executed, budget);
        const auto& objects = terminal.association.objects;
        const auto& additional = record.additional_object_inputs;
        budget.count(additional.size());
        budget.check(objects.size() >= n && objects.size() - n == additional.size(),
            ProjectionIssue::terminal_mismatch, "terminal object count differs from recorded inputs");
        for (std::size_t i = 0; i < objects.size(); ++i) {
            const auto& expected = i < additional.size() ? additional[i] : record.sources[i - additional.size()].object;
            budget.check(objects[i] == expected, ProjectionIssue::terminal_mismatch,
                         "terminal object order differs from recorded inputs");
        }
        budget.check(!terminal.association.output.empty() && !terminal.cache_file.empty(),
            ProjectionIssue::terminal_mismatch, "terminal output and cache reference are required");
        // Preflight all terminal paths BEFORE the existing owned projection.
        budget.paths(objects); budget.path(terminal.association.output); budget.path(terminal.cache_file);
        if constexpr (std::is_same_v<Terminal, LinkArtifactRecord>) {
            budget.paths(terminal.association.libraries); budget.paths(terminal.association.file_inputs);
            budget.paths(terminal.association.side_outputs);
            if (terminal.working_directory) budget.path(*terminal.working_directory);
        } else budget.path(terminal.working_directory);
        auto old_terminal = project_storage_references(terminal);
        auto stage = std::move(old_terminal.stages[0]);
        auto declared = std::move(stage.paths); stage.paths.clear();
        for (std::size_t i = 0; i < declared.size(); ++i) {
            const auto& ref = declared[i];
            if (ref.role == Role::input) {
                // Only the source-ordered local object slots are local edges.
                // An additional object/library cannot impersonate a producer.
                budget.path(ref.path);
                auto resolved = paths.resolve(ref.path, stage.working_directory);
                const bool local = i >= additional.size() && i < objects.size();
                if (local) budget.check(resolved && *resolved == object_keys[i - additional.size()],
                    ProjectionIssue::terminal_mismatch, "terminal object context differs from its compile stage");
                else if (resolved) paths.inputs.insert(*resolved);
                stage.paths.push_back(ref);
            } else paths.add_path(stage, ref.path, ref.role, true);
        }
        paths.finish();
        out.references.stages.push_back(std::move(stage));
        return out;
    } catch (const ProjectionRefusal& refusal) {
        return std::unexpected(refusal.error);
    }
}
} // namespace
std::expected<RecordedTargetStorageReferences, RecordedStorageProjectionError>
project_storage_references(const RecordedTargetResult& value, const StoragePathKey& key,
                           RecordedStorageProjectionLimits limits) {
    return project_recorded_target(value, value.record.link, value.result.link.linked, key, limits);
}
std::expected<RecordedTargetStorageReferences, RecordedStorageProjectionError>
project_storage_references(const RecordedStaticTargetResult& value, const StoragePathKey& key,
                           RecordedStorageProjectionLimits limits) {
    return project_recorded_target(value, value.record.archive, value.result.archive.archived, key, limits);
}
} // namespace mqb::orchestration
// END recorded ordinary-target storage projection
