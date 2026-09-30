#include "mqb/orchestration/ArtifactGenerationModel.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <type_traits>
#include <utility>

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
    void add(std::string_view s) {
        budget.check(s); budget.count(1);
        bytes += std::to_string(s.size()); bytes += ':'; bytes += s;
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
} // namespace

std::expected<ArtifactGenerationModel, ArtifactGenerationError>
model_artifact_generations(std::span<const ArtifactGenerationInput> inputs,
                           std::span<const ArtifactGenerationKey> retain,
                           const std::filesystem::path& lexical_root, const StoragePathKey& key) {
    using Issue = ArtifactGenerationIssue;
    using State = ArtifactGenerationState;
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
    } catch (const Invalid& e) { return std::unexpected(ArtifactGenerationError{e.message}); }
}
} // namespace mqb::orchestration
