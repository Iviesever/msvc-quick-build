#include "mqb/orchestration/ArtifactGenerationModel.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <type_traits>
#include <utility>

#include "mqb/orchestration/MsvcIncrementalStaticTargetCoordinator.hpp"
#include "mqb/orchestration/ArtifactGenerationArchive.hpp"
#include "ArtifactGenerationArchiveInternal.hpp"

namespace mqb::orchestration {
namespace {
struct Invalid { const char* message; };
struct Budget {
    std::size_t text = ArtifactGenerationLimits::text_bytes;
    std::size_t items = ArtifactGenerationLimits::items;
    void count(std::size_t n) {
        if (n > items) throw Invalid{"record item limit exceeded"};
        items -= n;
    }
    void check(std::string_view s) {
        if (s.size() > text) throw Invalid{"record text limit exceeded"};
        if (s.find('\0') != s.npos) throw Invalid{"NUL in identity evidence"};
        text -= s.size();
    }
};
// Unambiguous length framing, deliberately not hashing or normalizing recipes.
struct Fields {
    Budget& budget;
    std::string bytes;
    bool measure_only{false};
    void add(std::string_view s) {
        budget.check(s); budget.count(1);
        if (!measure_only) { bytes += std::to_string(s.size()); bytes += ':'; bytes += s; }
    }
    void add(const std::string& s) { add(std::string_view{s}); }
    void add(const char* s) { add(std::string_view{s}); }
    template<class T> requires (std::is_enum_v<T> || std::is_integral_v<T>)
    void add(T value) { add(std::to_string(static_cast<unsigned long long>(value))); }
    void add(const std::filesystem::path& p) {
        if (p.native().size() > budget.text) throw Invalid{"path limit exceeded"};
        const auto u = p.u8string();
        add(std::string_view{reinterpret_cast<const char*>(u.data()), u.size()});
    }
    template<class T> void add(const std::optional<T>& value) {
        add(value.has_value()); if (value) add(*value);
    }
    template<class T> void add(const std::vector<T>& values) {
        if (values.size() > budget.items) throw Invalid{"vector limit exceeded"};
        add(values.size()); for (const auto& v : values) add(v);
    }
    void add(const PrecompiledHeaderBinding& p) { add(p.header); add(p.artifact); add(p.role); }
    void add(const ExternalModuleProvider& p) { add(p.logical_name); add(p.interface_file); }
    void add(const CompilerOptions& o) {
        add(o.configuration); add(o.architecture); add(o.standard); add(o.runtime_library);
        add(o.link_time_code_generation); add(o.defines); add(o.include_directories);
        add(o.additional_arguments); add(o.precompiled_header); add(o.external_module_providers);
    }
    void add(const HeaderUnitIdentity& h) { add(h.header_name); add(h.lookup_method); }
    void add(const ModuleReference& r) { add(r.logical_name); add(r.interface_file); }
    void add(const HeaderUnitReference& r) { add(r.header_name); add(r.lookup_method); add(r.interface_file); }
    void add(const Artifact& r) { add(r.path); add(r.kind); }
    void add(const TranslationUnit& u) {
        add(u.source); add(u.kind); add(u.header_unit); add(u.dependencies);
        add(u.module_references); add(u.header_unit_references); add(u.outputs);
    }
    void add(const ModuleCompileArtifactRecord& r) {
        add(r.unit); add(r.compiler_options); add(r.dependencies); add(r.compile_cache);
        add(r.module_scan_output); add(r.working_directory);
        // force_rebuild and cache outcomes describe this call, not recipe identity.
    }
    void add(const LinkOptions& o) {
        add(o.configuration); add(o.architecture); add(o.target_kind); add(o.subsystem);
        add(o.link_time_code_generation); add(o.address_sanitizer_runtime_library);
        add(o.address_sanitizer_vcasan_runtime_library); add(o.fuzzer_runtime_library);
        add(o.msvc_openmp_runtime); add(o.library_directories); add(o.libraries); add(o.additional_arguments);
    }
    void add(const LinkArtifactRecord& r) {
        add(r.options); add(r.association.linker.linker); add(r.association.linker.version);
        add(r.association.linker.binary_stamp); add(r.association.signature.digest().high);
        add(r.association.signature.digest().low); add(r.association.objects); add(r.association.output);
        add(r.association.libraries); add(r.association.file_inputs); add(r.association.side_outputs);
        add(r.cache_file); add(r.working_directory);
    }
    void add(const ArchiveArtifactRecord& r) {
        add(r.architecture); add(r.link_time_code_generation); add(r.additional_arguments);
        add(r.association.librarian.librarian); add(r.association.librarian.version);
        add(r.association.librarian.binary_stamp); add(r.association.signature.digest().high);
        add(r.association.signature.digest().low); add(r.association.objects); add(r.association.output);
        add(r.cache_file); add(r.working_directory);
    }
};
void issue(ArtifactGenerationRecord& r, ArtifactGenerationIssue value) {
    if (std::find(r.issues.begin(), r.issues.end(), value) == r.issues.end()) r.issues.push_back(value);
}
bool same_key(const ArtifactGenerationRecord& r, const ArtifactGenerationKey& k) {
    return r.target == k.target && r.generation && *r.generation == k.generation;
}
bool valid_completion(ArtifactCompletion c, ArtifactCacheState s) {
    return (c == ArtifactCompletion::reused && s == ArtifactCacheState::reused) ||
        (c == ArtifactCompletion::executed && (s == ArtifactCacheState::saved || s == ArtifactCacheState::save_failed));
}
std::string utf8(const std::filesystem::path& path) {
    const auto s = path.u8string(); return {reinterpret_cast<const char*>(s.data()), s.size()};
}
bool snapshot_matches(const LinkFactSnapshot& s, const LinkArtifactRecord& r) {
    return validate_link_fact_snapshot(s).has_value() && s.completion == r.completion &&
        s.cache_state == r.cache_state && s.output == utf8(r.association.output) &&
        s.cache_file == utf8(r.cache_file) && s.signature == r.association.signature.digest() &&
        s.working_directory.has_value() == r.working_directory.has_value() &&
        (!r.working_directory || *s.working_directory == utf8(*r.working_directory)) &&
        s.linker_path == utf8(r.association.linker.linker) && s.linker_version == r.association.linker.version &&
        s.linker_stamp == r.association.linker.binary_stamp && s.configuration == r.options.configuration &&
        s.architecture == r.options.architecture && s.target_kind == r.options.target_kind;
}
bool configuration(BuildConfiguration c) { return c == BuildConfiguration::debug || c == BuildConfiguration::release; }
bool architecture(Architecture a) { return a == Architecture::x86 || a == Architecture::x64; }
bool runtime(const std::optional<RuntimeLibrary>& r) {
    return !r || *r == RuntimeLibrary::md || *r == RuntimeLibrary::mdd || *r == RuntimeLibrary::mt || *r == RuntimeLibrary::mtd;
}
bool options(const CompilerOptions& o) {
    return configuration(o.configuration) && architecture(o.architecture) && runtime(o.runtime_library) &&
        (o.standard == CppStandard::cpp14 || o.standard == CppStandard::cpp17 || o.standard == CppStandard::cpp20 ||
         o.standard == CppStandard::cpp23 || o.standard == CppStandard::latest) &&
        (!o.precompiled_header || o.precompiled_header->role == PrecompiledHeaderRole::create || o.precompiled_header->role == PrecompiledHeaderRole::use);
}
bool terminal_valid(const LinkArtifactRecord& r) {
    const auto& o = r.options;
    return configuration(o.configuration) && architecture(o.architecture) &&
        (o.target_kind == TargetKind::executable || o.target_kind == TargetKind::dynamic_library) &&
        (o.subsystem == LinkSubsystem::console || o.subsystem == LinkSubsystem::windows) &&
        runtime(o.address_sanitizer_runtime_library) && runtime(o.address_sanitizer_vcasan_runtime_library) && runtime(o.fuzzer_runtime_library) &&
        !r.association.output.empty() && !r.association.linker.linker.empty() &&
        !r.association.linker.version.empty() && !r.association.linker.binary_stamp.empty();
}
bool terminal_valid(const ArchiveArtifactRecord& r) {
    return architecture(r.architecture) && !r.association.output.empty() && !r.association.librarian.librarian.empty() &&
        !r.association.librarian.version.empty() && !r.association.librarian.binary_stamp.empty();
}
void identity_key(Budget& b, const ArtifactTargetKey& k) {
    b.check(k.project); b.check(k.target);
    if (k.project.empty() || k.target.empty()) throw Invalid{"explicit project and target keys required"};
}
void generation_key(Budget& b, const ArtifactGenerationKey& k) {
    identity_key(b, k.target); b.check(k.generation);
    if (k.generation.empty()) throw Invalid{"empty generation key"};
}
bool blocking(const ArtifactGenerationRecord& r) {
    return std::any_of(r.issues.begin(), r.issues.end(), [](auto i) { return i != ArtifactGenerationIssue::unresolved_path; });
}
std::expected<ArtifactGenerationModel, ArtifactGenerationError>
finish_model(ArtifactGenerationModel out, const std::vector<ArtifactStorageReferences>& projected,
             std::span<const ArtifactGenerationKey> retain,
             const std::filesystem::path& lexical_root, const StoragePathKey& key, Budget& budget) {
    using Issue = ArtifactGenerationIssue;
    using State = ArtifactGenerationState;
    StorageInventory empty;
    empty.artifact_root = lexical_root;
    auto joined = associate_artifact_storage(projected, empty, key);
    if (!joined) return std::unexpected(ArtifactGenerationError{joined.error().message});
    out.references = std::move(*joined);
    for (const auto& m : out.references.matches)
        if (!m.resolved_path) issue(out.records[m.record_index], Issue::unresolved_path);
    // Duplicate provenance and duplicate executed claims never silently win.
    for (std::size_t i = 0; i < out.records.size(); ++i) for (std::size_t j = i + 1; j < out.records.size(); ++j) {
        auto& a = out.records[i]; auto& b = out.records[j];
        if (a.source_id == b.source_id) { issue(a, Issue::duplicate_source); issue(b, Issue::duplicate_source); }
        if (a.completion == ArtifactCompletion::executed && b.completion == ArtifactCompletion::executed &&
            a.generation && b.generation && a.target == b.target && a.generation == b.generation) {
            issue(a, Issue::duplicate_generation); issue(b, Issue::duplicate_generation);
        }
    }
    for (std::size_t i = 0; i < out.records.size(); ++i) {
        auto& r = out.records[i];
        if (r.completion == ArtifactCompletion::executed && !blocking(r)) {
            r.origin_record = i; r.state = State::executed_claim;
        }
    }
    for (auto& r : out.records) {
        if (r.completion != ArtifactCompletion::reused || !r.generation) continue;
        std::vector<std::size_t> candidates;
        for (std::size_t j = 0; j < out.records.size(); ++j)
            if (out.records[j].completion == ArtifactCompletion::executed &&
                same_key(out.records[j], {r.target, *r.generation})) candidates.push_back(j);
        if (candidates.empty()) issue(r, Issue::missing_origin);
        else if (candidates.size() != 1) issue(r, Issue::ambiguous_origin);
        else {
            const auto j = candidates.front();
            if (r.recipe_evidence != out.records[j].recipe_evidence) issue(r, Issue::recipe_mismatch);
            if (blocking(out.records[j])) issue(r, Issue::ambiguous_origin);
            if (!blocking(r)) { r.origin_record = j; r.state = State::explicit_reuse; }
        }
    }
    for (auto& r : out.records) if (blocking(r)) {
        r.origin_record.reset(); r.state = State::unresolved;
        for (const auto i : r.issues)
            if (i == Issue::duplicate_source || i == Issue::duplicate_generation || i == Issue::ambiguous_origin ||
                i == Issue::invalid_recipe || i == Issue::recipe_mismatch || i == Issue::snapshot_mismatch || i == Issue::invalid_completion || i == Issue::mixed_reuse)
                r.state = State::conflicting;
    }
    std::set<std::size_t> retained;
    for (const auto& requested : retain) {
        generation_key(budget, requested);
        ArtifactRetentionSelection s; s.request = requested;
        bool mentioned = false;
        for (std::size_t i = 0; i < out.records.size(); ++i) if (same_key(out.records[i], requested)) {
            mentioned = true;
            if (out.records[i].completion == ArtifactCompletion::executed) s.executed_candidates.push_back(i);
        }
        if (s.executed_candidates.size() > 1) s.state = ArtifactRetentionState::ambiguous;
        else if (s.executed_candidates.size() == 1 && out.records[s.executed_candidates[0]].origin_record) {
            s.state = ArtifactRetentionState::selected;
            for (std::size_t i = 0; i < out.records.size(); ++i)
                if (out.records[i].origin_record == s.executed_candidates[0]) { s.associated_records.push_back(i); retained.insert(i); }
        } else if (mentioned) s.state = ArtifactRetentionState::unresolved;
        out.retention.push_back(std::move(s));
    }
    std::map<std::string, std::vector<std::size_t>> groups;
    for (std::size_t i = 0; i < out.references.matches.size(); ++i) {
        const auto& m = out.references.matches[i];
        if (!m.resolved_path) continue;
        const auto k = key(*m.resolved_path);
        if (k.empty()) throw Invalid{"empty path key"};
        budget.check(k); groups[k].push_back(i);
    }
    for (auto& [k, matches] : groups) {
        ArtifactGenerationPathGroup g; g.path_key = k; g.matches = std::move(matches);
        std::set<std::size_t> members, output_records, origins;
        bool input = false;
        for (const auto n : g.matches) {
            const auto& m = out.references.matches[n]; members.insert(m.record_index);
            const auto& p = out.references.records[m.record_index].stages[m.stage_index].paths[m.path_index];
            input |= p.role == ArtifactPathRole::input;
            if (p.role == ArtifactPathRole::declared_output) {
                output_records.insert(m.record_index);
                if (out.records[m.record_index].origin_record) origins.insert(*out.records[m.record_index].origin_record);
            }
        }
        g.records.assign(members.begin(), members.end());
        g.shared_reference = input && members.size() > 1;
        g.multiple_executed_generations = origins.size() > 1;
        for (const auto i : members) {
            if (retained.contains(i)) g.retained_records.push_back(i);
            g.unresolved_members |= !out.records[i].origin_record.has_value() ||
                std::find(out.records[i].issues.begin(), out.records[i].issues.end(), Issue::unresolved_path) != out.records[i].issues.end();
        }
        if (!output_records.empty()) for (const auto i : output_records) {
            const auto& a = out.records[*output_records.begin()]; const auto& b = out.records[i];
            g.different_target_or_recipe |= a.target != b.target || a.recipe_evidence != b.recipe_evidence;
        }
        out.paths.push_back(std::move(g));
    }
    return out;
}
} // namespace

std::expected<ArtifactGenerationModel, ArtifactGenerationError>
model_artifact_generations(std::span<const ArtifactGenerationInput> inputs,
                           std::span<const ArtifactGenerationKey> retain,
                           const std::filesystem::path& lexical_root, const StoragePathKey& key) {
    using Issue = ArtifactGenerationIssue;
    if (inputs.size() > ArtifactGenerationLimits::records || retain.size() > ArtifactGenerationLimits::records)
        return std::unexpected(ArtifactGenerationError{"record or retention limit exceeded"});
    try {
        ArtifactGenerationModel out;
        Budget budget;
        std::vector<ArtifactStorageReferences> projected;
        std::size_t stage_count = 0, path_count = 0;
        for (const auto& in : inputs) {
            identity_key(budget, in.target); budget.check(in.source_id);
            if (in.source_id.empty()) throw Invalid{"empty historical source identifier"};
            if (in.generation) {
                budget.check(*in.generation);
                if (in.generation->empty()) throw Invalid{"empty generation claim"};
            }
            ArtifactGenerationRecord r;
            r.source_id = in.source_id; r.target = in.target; r.generation = in.generation;
            Fields f{budget, {}}; f.add(in.record.index());
            if (!in.compiler || in.compiler->compiler.empty() || in.compiler->version.empty() || in.compiler->binary_stamp.empty())
                issue(r, Issue::missing_compiler);
            f.add(in.compiler.has_value());
            if (in.compiler) { f.add(in.compiler->compiler); f.add(in.compiler->version); f.add(in.compiler->binary_stamp); }
            if (!in.generation) issue(r, Issue::missing_generation);
            // Count/encode selected recipe evidence BEFORE the projection allocates.
            std::visit([&](const auto& target) {
                using T = std::decay_t<decltype(target)>;
                if (target.caller_label) { budget.check(target.caller_label->target); budget.check(target.caller_label->generation); }
                r.caller_label = target.caller_label;
                if constexpr (std::is_same_v<T, ModuleTargetArtifactRecord>) {
                    budget.count(target.scans.size()); budget.count(target.compiles.compiles.size());
                    budget.count(target.compiles.header_unit_compiles.size());
                    f.add(target.compiles.compiles.size());
                    for (const auto& c : target.compiles.compiles) {
                        f.add(c); if (!options(c.compiler_options)) issue(r, Issue::invalid_recipe);
                    }
                    f.add(target.compiles.header_unit_compiles.size());
                    for (const auto& c : target.compiles.header_unit_compiles) {
                        f.add(c); if (!options(c.compiler_options)) issue(r, Issue::invalid_recipe);
                    }
                    f.add(target.scans.size());
                    for (const auto& s : target.scans) {
                        const auto& recipe = s.scan.recipe;
                        f.add(recipe.toolchain.compiler); f.add(recipe.toolchain.version); f.add(recipe.toolchain.binary_stamp);
                        f.add(recipe.invocation.source); f.add(recipe.invocation.output_file);
                        f.add(recipe.invocation.options); f.add(recipe.invocation.kind); f.add(recipe.invocation.working_directory);
                        f.add(s.scan.compile_cache_reference);
                        if (!options(recipe.invocation.options)) issue(r, Issue::invalid_recipe);
                        // ProcessSpec/environment and P1689 documents remain owned by their
                        // original records; this is explicitly selected recipe evidence only.
                    }
                } else {
                    budget.count(target.sources.size()); f.add(target.compiler_options);
                    if (!options(target.compiler_options)) issue(r, Issue::invalid_recipe);
                    f.add(target.sources.size());
                    for (const auto& source : target.sources) {
                        f.add(source.source); f.add(source.object); f.add(source.dependencies); f.add(source.compile_cache);
                    }
                    f.add(target.additional_object_inputs);
                }
                const auto& terminal = [&]() -> const auto& {
                    if constexpr (std::is_same_v<T, StaticTargetArtifactRecord>) return target.archive;
                    else return target.link;
                }();
                f.add(terminal); r.completion = terminal.completion;
                if (!terminal_valid(terminal)) issue(r, Issue::invalid_recipe);
                if constexpr (!std::is_same_v<T, ModuleTargetArtifactRecord>) {
                    if constexpr (std::is_same_v<T, StaticTargetArtifactRecord>) {
                        if (target.compiler_options.architecture != terminal.architecture) issue(r, Issue::invalid_recipe);
                    } else if (target.compiler_options.architecture != terminal.options.architecture ||
                               target.compiler_options.configuration != terminal.options.configuration)
                        issue(r, Issue::invalid_recipe);
                }
                if (!valid_completion(terminal.completion, terminal.cache_state)) issue(r, Issue::invalid_completion);
                if (in.snapshot) {
                    const auto checked = encode_link_fact_snapshot(*in.snapshot);
                    if (!checked) issue(r, Issue::snapshot_mismatch);
                    else { budget.check(*checked); r.snapshot = *in.snapshot; }
                    if constexpr (std::is_same_v<T, StaticTargetArtifactRecord>) issue(r, Issue::snapshot_mismatch);
                    else if (!snapshot_matches(*in.snapshot, terminal)) issue(r, Issue::snapshot_mismatch);
                }
                projected.push_back(project_storage_references(target));
            }, in.record);
            const auto& refs = projected.back();
            if (refs.stages.size() > ArtifactGenerationLimits::stages - stage_count) throw Invalid{"stage limit exceeded"};
            stage_count += refs.stages.size(); f.add(refs.stages.size());
            for (const auto& s : refs.stages) {
                if (s.paths.size() > ArtifactGenerationLimits::paths - path_count) throw Invalid{"path limit exceeded"};
                path_count += s.paths.size();
                f.add(s.kind); f.add(s.configuration); f.add(s.working_directory); f.add(s.toolchain_source); f.add(s.paths.size());
                for (const auto& p : s.paths) { f.add(p.role); f.add(p.path); }
                if (s.completion != ArtifactCompletion::executed && s.completion != ArtifactCompletion::reused)
                    issue(r, Issue::invalid_completion);
                if (s.cache_state && !valid_completion(s.completion, *s.cache_state)) issue(r, Issue::invalid_completion);
                if (r.completion == ArtifactCompletion::reused && s.completion == ArtifactCompletion::executed)
                    issue(r, Issue::mixed_reuse);
            }
            r.recipe_evidence = std::move(f.bytes);
            out.records.push_back(std::move(r));
        }
        return finish_model(std::move(out), projected, retain, lexical_root, key, budget);
    } catch (const Invalid& e) { return std::unexpected(ArtifactGenerationError{e.message}); }
}

// BEGIN recorded generation model implementation
namespace {
template<class Recorded>
inline constexpr bool recorded_static_target = std::is_same_v<
    std::remove_cvref_t<decltype(std::declval<const Recorded&>().record)>, StaticTargetArtifactRecord>;
template<class Recorded>
const auto& recorded_terminal(const Recorded& value) {
    if constexpr (recorded_static_target<Recorded>) return value.record.archive;
    else return value.record.link;
}
template<class Recorded>
const Recorded& generation_record_value(const std::reference_wrapper<const Recorded>& value) {
    return value.get();
}
const ArchivedTargetClaim& generation_record_value(const ArchivedTargetClaim& value) { return value; }
const ArchivedStaticTargetClaim& generation_record_value(const ArchivedStaticTargetClaim& value) { return value; }
void compiler_fields(Fields& f, const ToolchainIdentity& t) {
    f.add(t.compiler); f.add(t.version); f.add(t.binary_stamp);
}
bool complete_compiler(const ToolchainIdentity& t) {
    return !t.compiler.empty() && !t.version.empty() && !t.binary_stamp.empty();
}
// The same selected-field traversal is used in count-only admission and actual
// encoding. ProcessResult, environment, timing and inspection plans are excluded.
template<class Recorded>
void recorded_recipe(Fields& f, const Recorded& value) {
    const auto& target = value.record;
    f.add(target.compiler_options); f.add(target.sources.size());
    for (const auto& s : target.sources) {
        f.add(s.source); f.add(s.object); f.add(s.dependencies); f.add(s.compile_cache);
    }
    f.add(target.additional_object_inputs);
    f.add(value.cache_evidence.compiles.size());
    for (const auto& e : value.cache_evidence.compiles) {
        f.add(e.request.unit); f.add(e.request.options);
        f.add(e.request.cache_file); f.add(e.request.source_dependencies_file);
        f.add(e.request.module_scan_output); f.add(e.request.working_directory);
        compiler_fields(f, e.inspection_toolchain.identity);
        const auto& c = e.cache_entry;
        compiler_fields(f, c.toolchain);
        f.add(c.source); f.add(c.kind); f.add(c.signature.digest().high); f.add(c.signature.digest().low);
        f.add(c.outputs); f.add(c.dependencies); f.add(c.include_search_roots);
        // Ordinary rich projection rejects module_scan; do not walk or copy a
        // module graph disguised as an ordinary input.
        f.add(c.module_scan.has_value());
    }
    f.add(recorded_terminal(value));
}
void snapshot_budget(Fields& f, const LinkFactSnapshot& s) {
    f.add(s.capture_label); f.add(s.output); f.add(s.cache_file); f.add(s.working_directory);
    f.add(s.linker_path); f.add(s.linker_version); f.add(s.linker_stamp);
    f.budget.count(s.warnings.size());
    for (const auto& w : s.warnings) { f.add(w.path); f.add(w.message); }
    const auto& o = s.observation;
    f.add(o.requested_path); f.add(o.physical_id); f.budget.count(o.issues.size());
    for (const auto& i : o.issues) { f.add(i.path); f.add(i.message); }
}
struct RecordedShapeBudget {
    std::size_t stages{ArtifactGenerationLimits::stages};
    std::size_t paths{ArtifactGenerationLimits::paths};
    void take(std::size_t& left, std::size_t n) {
        if (n > left) throw Invalid{"recorded generation shape limit exceeded"};
        left -= n;
    }
    // This charges projected references and relative expansion BEFORE projection.
    // A custom key's returned bytes are additionally bounded by that projector.
    void path(Fields& f, const std::filesystem::path& p,
              const std::optional<std::filesystem::path>& cwd) {
        take(paths, 1); f.add(ArtifactPathRole::metadata_reference); f.add(p);
        if (!p.is_absolute()) f.add(cwd);
    }
    template<class Recorded>
    void check(Fields& f, const Recorded& value) {
        const auto& target = value.record;
        take(stages, target.sources.size()); take(stages, 1); f.add(target.sources.size()+1);
        f.budget.count(target.sources.size());
        f.budget.count(value.cache_evidence.compiles.size());
        f.budget.count(value.result.compiles.size());
        for (const auto& e : value.cache_evidence.compiles) {
            const auto& cwd = e.request.working_directory;
            f.add(ArtifactStageKind::compile); f.add(std::optional{e.request.options.configuration});
            f.add(cwd); f.add(false);
            f.add(e.cache_entry.dependencies.size() + e.cache_entry.include_search_roots.size() + 4 +
                  (e.request.options.precompiled_header ? 2 : 0));
            path(f, e.request.unit.source, cwd);
            for (const auto& a : e.cache_entry.outputs) path(f, a.path, cwd);
            path(f, e.request.source_dependencies_file, cwd); path(f, e.request.cache_file, cwd);
            for (const auto& p : e.cache_entry.dependencies) path(f, p, cwd);
            for (const auto& p : e.cache_entry.include_search_roots) path(f, p, cwd);
            if (e.request.options.precompiled_header) {
                path(f, e.request.options.precompiled_header->header, cwd);
                path(f, e.request.options.precompiled_header->artifact, cwd);
            }
            if (e.save_error) { f.add(e.save_error->file); f.add(e.save_error->message); }
        }
        for (const auto& c : value.result.compiles) {
            f.add(c.source); f.budget.count(c.result.warnings.size());
            for (const auto& w : c.result.warnings) { f.add(w.path); f.add(w.message); }
        }
        if (target.caller_label) { f.add(target.caller_label->target); f.add(target.caller_label->generation); }
        const auto& t = recorded_terminal(value);
        const std::optional<std::filesystem::path> cwd = t.working_directory;
        f.add(ArtifactStageKind::link); f.add(std::optional{target.compiler_options.configuration});
        f.add(cwd); f.add(false); f.add(ArtifactGenerationLimits::paths);
        for (const auto& p : t.association.objects) path(f, p, cwd);
        path(f, t.association.output, cwd); path(f, t.cache_file, cwd);
        if constexpr (!recorded_static_target<Recorded>) {
            for (const auto& p : t.association.libraries) path(f, p, cwd);
            for (const auto& p : t.association.file_inputs) path(f, p, cwd);
            for (const auto& p : t.association.side_outputs) path(f, p, cwd);
        }
    }
};
// Live reference wrappers and owned archived claims share this entire admission,
// projection, recipe and lineage body. No intermediate invocation is fabricated.
template<class Input>
std::expected<RecordedArtifactGenerationModel, RecordedArtifactGenerationError>
model_recorded_generation_inputs(std::span<const Input> inputs,
                                 std::span<const ArtifactGenerationKey> retain,
                                 const std::filesystem::path& lexical_root, const StoragePathKey& key) {
    using Issue = ArtifactGenerationIssue;
    std::optional<std::size_t> current;
    if (inputs.size() > ArtifactGenerationLimits::records || retain.size() > ArtifactGenerationLimits::records)
        return std::unexpected(RecordedArtifactGenerationError{"record or retention limit exceeded", {}, {}});
    if (!key) return std::unexpected(RecordedArtifactGenerationError{"a pure platform path key is required", {}, {}});
    try {
        // Whole-batch count-only pass. No owned target/cache/recipe/context copies
        // and no path-key callback before every input passes this admission.
        Budget preflight;
        Fields count{preflight, {}, true};
        count.add(lexical_root);
        RecordedShapeBudget shape;
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            current = i;
            const auto& in = inputs[i];
            identity_key(preflight, in.target); count.add(in.source_id);
            if (in.source_id.empty()) throw Invalid{"empty historical source identifier"};
            count.add(in.generation);
            if (in.generation && in.generation->empty()) throw Invalid{"empty generation claim"};
            if (in.snapshot) {
                snapshot_budget(count, *in.snapshot);
                // The existing codec has its own bounded temporary allocation.
                // Budget its encoded bytes before copying any model snapshot.
                const auto encoded = encode_link_fact_snapshot(*in.snapshot);
                if (encoded) count.add(*encoded);
            }
            count.add("recorded-ordinary-v1"); count.add(in.record.index());
            std::visit([&](const auto& borrowed) {
                const auto& value = generation_record_value(borrowed);
                // Vector lengths are bounded before any per-element traversal.
                if (value.record.sources.size() >= ArtifactGenerationLimits::stages ||
                    value.cache_evidence.compiles.size() >= ArtifactGenerationLimits::stages ||
                    value.result.compiles.size() >= ArtifactGenerationLimits::stages)
                    throw Invalid{"recorded generation source limit exceeded"};
                recorded_recipe(count, value);
                shape.check(count, value);
            }, in.record);
        }
        current.reset();
        for (const auto& requested : retain) generation_key(preflight, requested);

        ArtifactGenerationModel model;
        std::vector<ArtifactStorageReferences> projected;
        std::vector<std::vector<CompileStorageContext>> contexts;
        Budget budget;
        std::size_t stage_count = 0, path_count = 0;
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            current = i;
            const auto& in = inputs[i];
            ArtifactGenerationRecord r;
            identity_key(budget, in.target); budget.check(in.source_id);
            if (in.generation) budget.check(*in.generation);
            r.source_id = in.source_id; r.target = in.target; r.generation = in.generation;
            if (!in.generation) issue(r, Issue::missing_generation);
            Fields f{budget, {}};
            f.add("recorded-ordinary-v1"); f.add(in.record.index());
            auto projection = std::visit([&](const auto& borrowed) {
                const auto& value = generation_record_value(borrowed);
                return project_storage_references(value, key, {
                    ArtifactGenerationLimits::stages - 1, ArtifactGenerationLimits::items,
                    ArtifactGenerationLimits::text_bytes});
            }, in.record);
            if (!projection) return std::unexpected(RecordedArtifactGenerationError{
                projection.error().message, i, projection.error()});
            std::visit([&](const auto& borrowed) {
                const auto& value = generation_record_value(borrowed);
                const auto& target = value.record;
                recorded_recipe(f, value);
                r.caller_label = target.caller_label;
                if (!options(target.compiler_options)) issue(r, Issue::invalid_recipe);
                for (const auto& e : value.cache_evidence.compiles) {
                    if (!complete_compiler(e.inspection_toolchain.identity) || !complete_compiler(e.cache_entry.toolchain))
                        issue(r, Issue::missing_compiler);
                    if (!options(e.request.options)) issue(r, Issue::invalid_recipe);
                }
                const auto& terminal = recorded_terminal(value);
                r.completion = terminal.completion;
                if (!terminal_valid(terminal)) issue(r, Issue::invalid_recipe);
                using Recorded = std::remove_cvref_t<decltype(value)>;
                if constexpr (recorded_static_target<Recorded>) {
                    if (target.compiler_options.architecture != terminal.architecture) issue(r, Issue::invalid_recipe);
                } else if (target.compiler_options.architecture != terminal.options.architecture ||
                           target.compiler_options.configuration != terminal.options.configuration)
                    issue(r, Issue::invalid_recipe);
                if (in.snapshot) {
                    const auto checked = encode_link_fact_snapshot(*in.snapshot);
                    if (!checked) issue(r, Issue::snapshot_mismatch);
                    else { budget.check(*checked); r.snapshot = *in.snapshot; }
                    if constexpr (recorded_static_target<Recorded>) issue(r, Issue::snapshot_mismatch);
                    else if (!snapshot_matches(*in.snapshot, terminal)) issue(r, Issue::snapshot_mismatch);
                }
            }, in.record);
            const auto& refs = projection->references;
            if (refs.stages.size() > ArtifactGenerationLimits::stages - stage_count) throw Invalid{"stage limit exceeded"};
            stage_count += refs.stages.size(); f.add(refs.stages.size());
            for (const auto& s : refs.stages) {
                if (s.paths.size() > ArtifactGenerationLimits::paths - path_count) throw Invalid{"path limit exceeded"};
                path_count += s.paths.size();
                f.add(s.kind); f.add(s.configuration); f.add(s.working_directory); f.add(s.toolchain_source); f.add(s.paths.size());
                for (const auto& p : s.paths) { f.add(p.role); f.add(p.path); }
                if (!s.cache_state || !valid_completion(s.completion, *s.cache_state)) issue(r, Issue::invalid_completion);
                if (r.completion == ArtifactCompletion::reused && s.completion == ArtifactCompletion::executed)
                    issue(r, Issue::mixed_reuse);
            }
            r.recipe_evidence = std::move(f.bytes);
            projected.push_back(std::move(projection->references));
            contexts.push_back(std::move(projection->compiles));
            model.records.push_back(std::move(r));
        }
        current.reset();
        auto finished = finish_model(std::move(model), projected, retain, lexical_root, key, budget);
        if (!finished) return std::unexpected(RecordedArtifactGenerationError{finished.error().message, {}, {}});
        return RecordedArtifactGenerationModel{std::move(*finished), std::move(contexts)};
    } catch (const Invalid& e) {
        return std::unexpected(RecordedArtifactGenerationError{e.message, current, {}});
    }
}

template<class Recorded>
auto archive_recorded_claim(const Recorded& value) {
    using Claim = std::conditional_t<recorded_static_target<Recorded>,
        ArchivedStaticTargetClaim, ArchivedTargetClaim>;
    Claim out;
    out.record = value.record;
    out.result.any_compiled = value.result.any_compiled;
    if constexpr (recorded_static_target<Recorded>) {
        out.result.archive.archived = value.result.archive.archived;
        out.result.archive.warnings = value.result.archive.warnings;
    } else {
        out.result.link.linked = value.result.link.linked;
        out.result.link.warnings = value.result.link.warnings;
    }
    out.result.compiles.reserve(value.result.compiles.size());
    for (const auto& c : value.result.compiles)
        out.result.compiles.push_back({c.source, {c.result.compiled, c.result.warnings}});
    out.cache_evidence.compiles.reserve(value.cache_evidence.compiles.size());
    for (const auto& e : value.cache_evidence.compiles) {
        // The common strict projector has already rejected module/scan graphs.
        // This selected identity wrapper cannot copy MsvcToolchain::environment.
        out.cache_evidence.compiles.push_back({e.request, {e.inspection_toolchain.identity},
            e.cache_entry, e.state, e.save_error});
    }
    return out;
}
} // namespace

std::expected<RecordedArtifactGenerationModel, RecordedArtifactGenerationError>
model_recorded_artifact_generations(std::span<const RecordedArtifactGenerationInput> inputs,
                                   std::span<const ArtifactGenerationKey> retain,
                                   const std::filesystem::path& lexical_root, const StoragePathKey& key) {
    return model_recorded_generation_inputs(inputs, retain, lexical_root, key);
}

std::expected<RecordedArtifactGenerationModel, RecordedArtifactGenerationError>
model_archived_artifact_generations(const ArtifactGenerationArchive& archive, const StoragePathKey& key) {
    return model_recorded_generation_inputs(std::span<const ArchivedArtifactGenerationInput>{archive.records},
        archive.retain, archive.lexical_root, key);
}

std::expected<ArtifactGenerationArchive, ArtifactGenerationArchiveError>
project_artifact_generation_archive(std::span<const RecordedArtifactGenerationInput> inputs,
                                    std::span<const ArtifactGenerationKey> retain,
                                    const std::filesystem::path& lexical_root, const StoragePathKey& key) {
    const auto admitted = detail::preflight_recorded_generation_archive(inputs, retain, lexical_root);
    if (!admitted) return std::unexpected(admitted.error());
    // Strict model admission validates the original independent source/result/
    // request/cache facts before the first owning archived claim is copied.
    const auto validated = model_recorded_artifact_generations(inputs, retain, lexical_root, key);
    if (!validated) return std::unexpected(ArtifactGenerationArchiveError{
        ArtifactGenerationArchiveErrorCode::invalid_evidence, validated.error().message, 0, validated.error()});

    ArtifactGenerationArchive out;
    out.lexical_root = lexical_root;
    out.retain.assign(retain.begin(), retain.end());
    out.records.reserve(inputs.size());
    for (const auto& in : inputs) {
        using Claims = std::variant<ArchivedTargetClaim, ArchivedStaticTargetClaim>;
        auto claim = std::visit([](const auto& borrowed) -> Claims {
            return archive_recorded_claim(borrowed.get());
        }, in.record);
        out.records.push_back({in.source_id, in.target, in.generation, std::move(claim), in.snapshot});
    }
    return out;
}
// END recorded generation model implementation
} // namespace mqb::orchestration
