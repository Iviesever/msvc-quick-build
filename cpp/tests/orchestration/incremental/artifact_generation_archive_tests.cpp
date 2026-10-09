#include <iostream>

#if !__has_include("mqb/orchestration/ArtifactGenerationArchive.hpp")
int main() {
    std::cerr << "FAIL: artifact generation archive API is absent; owned projection and binary round-trip are unavailable\n";
    return 1;
}
#else
#include "mqb/orchestration/ArtifactGenerationArchive.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <type_traits>

// This executable alone observes ordinary string/vector allocations. Probes
// cover explicit observer controls and two malformed decodes after inputs exist.
namespace archive_allocation_probe {
bool active{};
std::size_t largest_request{};
void observe(std::size_t size) noexcept {
    if(active && size>largest_request)largest_request=size;
}
struct Scope {
    Scope() {largest_request=0;active=true;}
    ~Scope() {active=false;}
};
}
void* operator new(std::size_t size) {
    archive_allocation_probe::observe(size);
    if(auto* memory=std::malloc(size ? size : 1))return memory;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) {return ::operator new(size);}
void operator delete(void* memory) noexcept {std::free(memory);}
void operator delete[](void* memory) noexcept {std::free(memory);}
void operator delete(void* memory,std::size_t) noexcept {std::free(memory);}
void operator delete[](void* memory,std::size_t) noexcept {std::free(memory);}

namespace {
using namespace mqb;
using namespace mqb::orchestration;
namespace fs = std::filesystem;
using State = ArtifactGenerationState;
using Issue = ArtifactGenerationIssue;
using Error = ArtifactGenerationArchiveErrorCode;
unsigned checks{};

void check(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
template<class T, class E>
T take(std::expected<T,E> value, const char* message) {
    if (!value) throw std::runtime_error(std::string(message)+": "+value.error().message);
    return std::move(*value);
}
fs::path root() {
#ifdef _WIN32
    return "C:/generation-archive";
#else
    return "/generation-archive";
#endif
}
std::string utf8(const fs::path& path) {
    const auto text=path.u8string();
    return {reinterpret_cast<const char*>(text.data()),text.size()};
}
const auto lexical_key=[](const fs::path& path) { return utf8(path); };

// A small real value fixture, with two independently captured compiler contexts.
// No compiler, observer, filesystem, process runner or clock is invoked.
RecordedTargetResult fixture(bool dll=false) {
    RecordedTargetResult value{.record={.link={.completion=ArtifactCompletion::executed,
        .cache_state=ArtifactCacheState::saved,
        .association={.linker={root()/"link.exe","link-v1","link-stamp"},
            .signature=BuildSignature::from_digest({0x1020304050607080ULL,0x90abcdef01234567ULL})},
        .working_directory=root()}}};
    auto& record=value.record;
    auto& options=record.compiler_options;
    record.caller_label=ArtifactGenerationLabel{"label-only","not-the-generation"};
    options.runtime_library=RuntimeLibrary::mdd;
    options.link_time_code_generation=true;
    options.defines={"FIRST=1","SECOND=2","FIRST=1"};
    options.include_directories={root()/"includes",root()/"includes"};
    options.additional_arguments={"/O1","/O2","/O1"};
    options.precompiled_header=PrecompiledHeaderBinding{root()/"pch.hpp",root()/"upstream.pch",PrecompiledHeaderRole::use};
    options.external_module_providers={{"ReadOnly",root()/"provider.ifc"}};
    record.additional_object_inputs={root()/"prebuilt.obj"};
    auto& link=record.link;
    link.options.target_kind=dll ? TargetKind::dynamic_library : TargetKind::executable;
    link.options.subsystem=LinkSubsystem::windows;
    link.options.link_time_code_generation=true;
    link.options.address_sanitizer_runtime_library=RuntimeLibrary::mdd;
    link.options.address_sanitizer_vcasan_runtime_library=RuntimeLibrary::mdd;
    link.options.fuzzer_runtime_library=RuntimeLibrary::mdd;
    link.options.msvc_openmp_runtime=true;
    link.options.library_directories={root()/"libs",root()/"libs"};
    link.options.libraries={"alpha.lib","beta.lib","alpha.lib"};
    link.options.additional_arguments={"/DEBUG","/OPT:REF","/DEBUG"};
    link.association.output=root()/(dll ? "plugin.dll" : "application.exe");
    link.association.objects=record.additional_object_inputs;
    link.association.libraries={root()/"imported.lib"};
    link.association.file_inputs={root()/"exports.def"};
    link.association.side_outputs=dll ? std::vector{root()/"plugin.lib",root()/"plugin.exp"}
                                      : std::vector{root()/"application.pdb"};
    link.cache_file=root()/"target.linkcache";
    value.result.link.linked=true;
    value.result.link.warnings.push_back({IncrementalLinkWarningCode::file_snapshot_failed,
        root()/"unavailable-input.lib","historical link warning"});
    value.result.link.warnings.push_back({IncrementalLinkWarningCode::cache_load_failed,
        root()/"old.linkcache","historical link load warning"});
    value.result.link.process=process::ProcessResult{.stdout_text="PRIVATE-LINK-STDOUT",.stderr_text="PRIVATE-LINK-STDERR"};
    value.result.any_compiled=true;
    for (unsigned i=0;i<2;++i) {
        const auto name=std::string("unit-")+std::to_string(i);
        const auto source=root()/(name+".cpp"),object=root()/(name+".obj");
        const auto dependencies=root()/(name+".d"),cache=root()/(name+".compilecache");
        record.sources.push_back({source,object,dependencies,cache,ArtifactCompletion::executed,false});
        TargetCompileResult completion{.source=source};
        completion.result.compiled=true;
        completion.result.process=process::ProcessResult{.stdout_text="PRIVATE-COMPILE-STDOUT",.stderr_text="PRIVATE-COMPILE-STDERR"};
        value.result.compiles.push_back(std::move(completion));
        const ToolchainIdentity inspection{root()/"cl.exe","inspection-v"+std::to_string(i),"inspection-stamp-"+name};
        const ToolchainIdentity execution{root()/"cl.exe","execution-v"+std::to_string(i),"execution-stamp-"+name};
        value.cache_evidence.compiles.push_back({
            .request={.unit={.source=source,.outputs={{object,ArtifactKind::object}}},
                .options=options,.cache_file=cache,.source_dependencies_file=dependencies,
                .working_directory=root()/("cwd-"+name)},
            .inspection_toolchain={.identity=inspection,.environment={{"PRIVATE-ENV-NAME","PRIVATE-ENV-VALUE"}}},
            .cache_entry={.source=source,.toolchain=execution,
                .signature=BuildSignature::from_digest({0xffffffffffffffffULL,0x8000000000000000ULL+i}),
                .outputs={{object,ArtifactKind::object}},
                .dependencies={root()/"shared.hpp",root()/fs::path{u8"\u8cc7\u6599.hpp"},root()/"shared.hpp"},
                .include_search_roots={root()/"include-first",root()/"include-second",root()/"include-first"}},
            .state=CompileCacheEvidenceState::saved});
        link.association.objects.push_back(object);
    }
    return value;
}
template<class T>
void reuse_source(T& value,std::size_t index) {
    value.record.sources[index].completion=ArtifactCompletion::reused;
    value.result.compiles[index].result.compiled=false;
    value.cache_evidence.compiles[index].state=CompileCacheEvidenceState::reused;
}
void reuse(RecordedTargetResult& value) {
    for(std::size_t i=0;i<value.record.sources.size();++i) reuse_source(value,i);
    value.result.any_compiled=false;value.result.link.linked=false;
    value.record.link.completion=ArtifactCompletion::reused;
    value.record.link.cache_state=ArtifactCacheState::reused;
}
RecordedStaticTargetResult static_fixture() {
    const auto value=fixture();
    RecordedStaticTargetResult result{.record={.archive={.completion=ArtifactCompletion::executed,
        .cache_state=ArtifactCacheState::saved,.association={.librarian={root()/"lib.exe","lib-v1","lib-stamp"},
            .signature=BuildSignature::from_digest({77,88}),.objects=value.record.link.association.objects,
            .output=root()/"library.lib"},.architecture=Architecture::x64,.link_time_code_generation=true,
        .additional_arguments={"/NOLOGO","/IGNORE:4221","/NOLOGO"},
        .cache_file=root()/"library.archivecache",.working_directory=root()}}};
    result.record.caller_label=value.record.caller_label;
    result.record.compiler_options=value.record.compiler_options;
    result.record.sources=value.record.sources;
    result.record.additional_object_inputs=value.record.additional_object_inputs;
    result.cache_evidence=value.cache_evidence;
    result.result.compiles=value.result.compiles;
    result.result.any_compiled=true;result.result.archive.archived=true;
    result.result.archive.warnings.push_back({IncrementalArchiveWarningCode::file_snapshot_failed,
        root()/"old-input.lib","historical archive warning"});
    result.result.archive.warnings.push_back({IncrementalArchiveWarningCode::cache_load_failed,
        root()/"old.archivecache","historical archive load warning"});
    result.result.archive.process=process::ProcessResult{.stdout_text="PRIVATE-ARCHIVE-STDOUT"};
    return result;
}
template<class T>
RecordedArtifactGenerationInput input(const T& value,std::string id="origin-record-000",std::string generation="generation-001") {
    return {std::move(id),{"project","target"},std::move(generation),std::cref(value),std::nullopt};
}
ArtifactGenerationKey retain(std::string generation="generation-001") {
    return {{"project","target"},std::move(generation)};
}
ArtifactGenerationArchive project(const std::vector<RecordedArtifactGenerationInput>& records,
                                  const std::vector<ArtifactGenerationKey>& retained={}) {
    return take(project_artifact_generation_archive(records,retained,root(),lexical_key),"project");
}
std::string encode(const ArtifactGenerationArchive& archive) {
    return take(encode_artifact_generation_archive(archive,lexical_key),"encode");
}
RecordedArtifactGenerationModel archived_model(const ArtifactGenerationArchive& archive) {
    return take(model_archived_artifact_generations(archive,lexical_key),"archived model");
}
bool has(const ArtifactGenerationRecord& record,Issue issue) {
    return std::find(record.issues.begin(),record.issues.end(),issue)!=record.issues.end();
}
template<class T>
void same_warnings(const std::vector<T>& a,const std::vector<T>& b) {
    check(a.size()==b.size(),"terminal warning count");
    for(std::size_t i=0;i<a.size();++i)
        check(a[i].code==b[i].code && a[i].path==b[i].path && a[i].message==b[i].message,"ordered terminal warning fields");
}
void same_label(const std::optional<ArtifactGenerationLabel>& a,const std::optional<ArtifactGenerationLabel>& b) {
    check(a.has_value()==b.has_value(),"caller label presence");
    if(a)check(a->target==b->target && a->generation==b->generation,"caller label values");
}
void same_identity(const ToolchainIdentity& a,const ToolchainIdentity& b) {
    check(a.compiler==b.compiler && a.version==b.version && a.binary_stamp==b.binary_stamp,"captured toolchain identity");
}
// Compare every member of the returned semantic model, rather than merely
// asking whether re-encoding two objects happens to produce the same bytes.
void same_model(const RecordedArtifactGenerationModel& left,const RecordedArtifactGenerationModel& right) {
    const auto& a=left.model;const auto& b=right.model;
    check(a.records.size()==b.records.size(),"model record count");
    for(std::size_t i=0;i<a.records.size();++i) {
        const auto& x=a.records[i];const auto& y=b.records[i];
        check(x.source_id==y.source_id && x.target==y.target && x.generation==y.generation &&
            x.completion==y.completion && x.snapshot==y.snapshot && x.recipe_evidence==y.recipe_evidence &&
            x.state==y.state && x.origin_record==y.origin_record && x.issues==y.issues,"record lineage, recipe and issues");
        same_label(x.caller_label,y.caller_label);
    }
    check(a.retention.size()==b.retention.size(),"retention count");
    for(std::size_t i=0;i<a.retention.size();++i) {
        const auto& x=a.retention[i];const auto& y=b.retention[i];
        check(x.request==y.request && x.state==y.state && x.executed_candidates==y.executed_candidates &&
            x.associated_records==y.associated_records,"retention requests and indices");
    }
    check(a.paths.size()==b.paths.size(),"path group count");
    for(std::size_t i=0;i<a.paths.size();++i) {
        const auto& x=a.paths[i];const auto& y=b.paths[i];
        check(x.path_key==y.path_key && x.matches==y.matches && x.records==y.records &&
            x.retained_records==y.retained_records && x.shared_reference==y.shared_reference &&
            x.multiple_executed_generations==y.multiple_executed_generations &&
            x.different_target_or_recipe==y.different_target_or_recipe && x.unresolved_members==y.unresolved_members,
            "path collision and retention relationships");
    }
    const auto& ar=a.references;const auto& br=b.references;
    check(ar.records.size()==br.records.size() && ar.matches.size()==br.matches.size() &&
        ar.row_matches==br.row_matches,"reference and match counts");
    check(ar.observation.artifact_root==br.observation.artifact_root &&
        ar.observation.root_exists==br.observation.root_exists &&
        ar.observation.entries.empty() && br.observation.entries.empty() &&
        ar.observation.references.empty() && br.observation.references.empty() &&
        ar.observation.issues.empty() && br.observation.issues.empty(),"no invented filesystem observation");
    for(std::size_t i=0;i<ar.records.size();++i) {
        const auto& x=ar.records[i];const auto& y=br.records[i];same_label(x.caller_label,y.caller_label);
        check(x.stages.size()==y.stages.size(),"stage count");
        for(std::size_t j=0;j<x.stages.size();++j) {
            const auto& p=x.stages[j];const auto& q=y.stages[j];
            check(p.kind==q.kind && p.completion==q.completion && p.cache_state==q.cache_state &&
                p.source_has_warnings==q.source_has_warnings && p.configuration==q.configuration &&
                p.working_directory==q.working_directory && p.toolchain_source==q.toolchain_source &&
                p.paths.size()==q.paths.size(),"stage outcome and context");
            for(std::size_t k=0;k<p.paths.size();++k)
                check(p.paths[k].path==q.paths[k].path && p.paths[k].role==q.paths[k].role,"ordered path roles");
        }
    }
    for(std::size_t i=0;i<ar.matches.size();++i) {
        const auto& x=ar.matches[i];const auto& y=br.matches[i];
        check(x.record_index==y.record_index && x.stage_index==y.stage_index && x.path_index==y.path_index &&
            x.resolved_path==y.resolved_path && x.state==y.state && x.observed_rows==y.observed_rows &&
            x.same_observed_file_rows==y.same_observed_file_rows &&
            x.observed_identity_metadata_conflict==y.observed_identity_metadata_conflict,"reference match indices");
    }
    check(left.compiles.size()==right.compiles.size(),"compile context record count");
    for(std::size_t i=0;i<left.compiles.size();++i) {
        check(left.compiles[i].size()==right.compiles[i].size(),"compile context source count");
        for(std::size_t j=0;j<left.compiles[i].size();++j) {
            const auto& x=left.compiles[i][j];const auto& y=right.compiles[i][j];
            same_identity(x.inspection_toolchain,y.inspection_toolchain);same_identity(x.cache_toolchain,y.cache_toolchain);
            check(x.captured_signature==y.captured_signature && x.toolchains_differ==y.toolchains_differ &&
                x.force_rebuild==y.force_rebuild && x.save_error.has_value()==y.save_error.has_value() &&
                x.warnings.size()==y.warnings.size(),"compile signature and outcomes");
            if(x.save_error)check(x.save_error->code==y.save_error->code && x.save_error->file==y.save_error->file &&
                x.save_error->offset==y.save_error->offset && x.save_error->message==y.save_error->message,"original save failure");
            for(std::size_t k=0;k<x.warnings.size();++k)
                check(x.warnings[k].code==y.warnings[k].code && x.warnings[k].path==y.warnings[k].path &&
                    x.warnings[k].message==y.warnings[k].message,"compile warning values and order");
        }
    }
}
ArtifactGenerationArchive roundtrip(const std::vector<RecordedArtifactGenerationInput>& records,
                                    const std::vector<ArtifactGenerationKey>& retained={}) {
    const auto live=take(model_recorded_artifact_generations(records,retained,root(),lexical_key),"live model");
    const auto projected=project(records,retained);
    same_model(live,archived_model(projected));
    const auto bytes=encode(projected);
    const auto decoded=take(decode_artifact_generation_archive(bytes,lexical_key),"decode");
    same_model(live,archived_model(decoded));
    check(decoded.lexical_root==root() && decoded.retain==retained,"archive owns root and ordered retention requests");
    check(encode(decoded)==bytes && encode(project(records,retained))==bytes,"canonical deterministic round-trip");
    return decoded;
}

template<class T> concept HasProcess = requires(T value) { value.process; };
template<class T> concept HasEnvironment = requires(T value) { value.environment; };
template<class T> concept HasPlan = requires(T value) { value.plan; };
template<class T> concept HasTimings = requires(T value) { value.timings; };

void roundtrip_contracts() {
    static_assert(!ArtifactGenerationArchive::producer_identity_verified && !ArtifactGenerationArchive::current_content_verified &&
        !ArtifactGenerationArchive::complete_producer_inventory && !ArtifactGenerationArchive::deletion_authorized);
    static_assert(!HasProcess<ArchivedCompileCompletion> && !HasProcess<ArchivedLinkCompletion> &&
        !HasProcess<ArchivedArchiveCompletion> && !HasEnvironment<ArchivedInspectionToolchain> &&
        !HasPlan<ArchivedCompileCompletion> && !HasTimings<ArchivedTargetCompletion>);
    static_assert(!std::is_convertible_v<ArchivedTargetClaim,RecordedTargetResult> &&
        !std::is_convertible_v<ArchivedStaticTargetClaim,RecordedStaticTargetResult>);
    check(roundtrip({}).records.empty(),"empty archive does not create a producer");
    auto cold=fixture(),warm=cold;reuse(warm);
    auto archived=roundtrip({input(cold),input(warm,"warm-record-001")},{retain(),retain(),retain("absent")});
    auto model=archived_model(archived);
    check(model.model.records[0].state==State::executed_claim && model.model.records[1].state==State::explicit_reuse &&
        model.model.records[1].origin_record==0,"cold and warm claims share one explicit origin");
    check(model.model.retention[0].associated_records==std::vector<std::size_t>{0,1} &&
        model.model.retention[2].state==ArtifactRetentionState::missing,"retention remains explicit");
    const auto& first=std::get<ArchivedTargetClaim>(archived.records[0].record);
    check(first.record.compiler_options.defines==cold.record.compiler_options.defines &&
        first.record.compiler_options.additional_arguments==cold.record.compiler_options.additional_arguments &&
        first.cache_evidence.compiles[0].cache_entry.dependencies==cold.cache_evidence.compiles[0].cache_entry.dependencies,
        "ordered and duplicate selected values survive");
    check(first.cache_evidence.compiles[1].cache_entry.signature==cold.cache_evidence.compiles[1].cache_entry.signature &&
        first.result.link.warnings[0].message=="historical link warning","full-width digest and terminal warnings survive");
    same_warnings(first.result.link.warnings,cold.result.link.warnings);
    auto reverse=roundtrip({input(warm,"warm-record-001"),input(cold)},{retain()});
    check(archived_model(reverse).model.records[0].origin_record==1 && encode(reverse)!=encode(archived),"input order is not time order");
    auto dll=fixture(true);const auto dll_archive=roundtrip({input(dll)});
    check(std::get<ArchivedTargetClaim>(dll_archive.records[0].record).record.link.association.side_outputs==
        std::vector{root()/"plugin.lib",root()/"plugin.exp"},"DLL import-library and EXP remain declared outputs");
    auto library=static_fixture(),warm_library=library;
    for(std::size_t i=0;i<2;++i)reuse_source(warm_library,i);
    warm_library.result.any_compiled=false;warm_library.result.archive.archived=false;
    warm_library.record.archive.completion=ArtifactCompletion::reused;
    warm_library.record.archive.cache_state=ArtifactCacheState::reused;
    auto static_archive=roundtrip({input(library),input(warm_library,"warm-library")},{retain()});
    const auto& static_claim=std::get<ArchivedStaticTargetClaim>(static_archive.records[0].record);
    check(static_claim.result.archive.warnings[0].message=="historical archive warning" &&
        static_claim.record.archive.additional_arguments==library.record.archive.additional_arguments,"LIB terminal selection");
    same_warnings(static_claim.result.archive.warnings,library.result.archive.warnings);
    check(!archived_model(static_archive).model.references.records[0].stages.back().configuration,"LIB configuration remains unknown");
    auto partial=cold;reuse_source(partial,0);
    check(archived_model(roundtrip({input(partial)})).model.references.records[0].stages[0].completion==ArtifactCompletion::reused,
        "partial source reuse survives");
    partial.result.link.linked=false;partial.record.link.completion=ArtifactCompletion::reused;
    partial.record.link.cache_state=ArtifactCacheState::reused;
    check(has(archived_model(roundtrip({input(partial)})).model.records[0],Issue::mixed_reuse),"contradictory partial target stays conflicting");
    auto save_failed=cold;
    auto& evidence=save_failed.cache_evidence.compiles[1];
    evidence.state=CompileCacheEvidenceState::save_failed;evidence.request.force_rebuild=true;
    evidence.save_error=CompileCacheFileError{.code=CompileCacheFileErrorCode::replace_failed,.file=evidence.request.cache_file,
        .offset=31,.message="destination locked"};
    save_failed.result.compiles[1].result.warnings.push_back({IncrementalCompileWarningCode::cache_save_failed,evidence.request.cache_file,"destination locked"});
    save_failed.record.sources[1].has_warnings=true;
    const auto failure=archived_model(roundtrip({input(save_failed),input(warm,"warm-record-001")}));
    check(failure.compiles[0][1].force_rebuild && failure.compiles[0][1].save_error->offset==31 &&
        failure.model.references.records[0].stages[1].cache_state==ArtifactCacheState::save_failed &&
        failure.model.records[1].state==State::explicit_reuse,"save failure and force are retained without changing recipe identity");
    auto no_generation=input(cold);no_generation.generation.reset();
    check(has(archived_model(roundtrip({no_generation})).model.records[0],Issue::missing_generation),"missing generation is not reconstructed");
    check(has(archived_model(roundtrip({input(warm)})).model.records[0],Issue::missing_origin),"reuse without producer stays unresolved");
    auto no_compiler=cold;no_compiler.cache_evidence.compiles[1].cache_entry.toolchain.binary_stamp.clear();
    check(has(archived_model(roundtrip({input(no_compiler)})).model.records[0],Issue::missing_compiler),"incomplete cache compiler remains unknown");
    const auto duplicate=archived_model(roundtrip({input(cold),input(cold,"duplicate-producer"),input(warm,"warm-record-001")},{retain()}));
    check(duplicate.model.retention[0].state==ArtifactRetentionState::ambiguous &&
        has(duplicate.model.records[2],Issue::ambiguous_origin),"duplicate execution claims are not deduplicated");
    check(has(archived_model(roundtrip({input(cold),input(cold)})).model.records[0],Issue::duplicate_source),"duplicate record IDs stay visible");
    auto changed=warm;changed.cache_evidence.compiles[1].inspection_toolchain.identity.binary_stamp="another-captured-compiler";
    check(has(archived_model(roundtrip({input(cold),input(changed,"changed-reuse")})).model.records[1],Issue::recipe_mismatch),
        "per-source compiler mismatch cannot attach to an origin");
    const auto overwrite=archived_model(roundtrip({input(cold),input(cold,"new-generation","generation-002")},{retain()}));
    check(std::any_of(overwrite.model.paths.begin(),overwrite.model.paths.end(),[](const auto& path) {
        return path.multiple_executed_generations && path.retained_records==std::vector<std::size_t>{0};
    }),"overlapping generations remain possible overwrites, never garbage");
    auto relative=cold;
    for(std::size_t i=0;i<relative.record.sources.size();++i) {
        auto& source=relative.record.sources[i];auto& e=relative.cache_evidence.compiles[i];
        source.source=source.source.filename();source.object=source.object.filename();
        source.dependencies=source.dependencies.filename();source.compile_cache=source.compile_cache.filename();
        relative.result.compiles[i].source=source.source;
        e.request.unit.source=source.source;e.cache_entry.source=source.source;
        e.request.unit.outputs[0].path=source.object;e.cache_entry.outputs[0].path=source.object;
        e.request.cache_file=source.compile_cache;e.request.source_dependencies_file=source.dependencies;
        e.request.working_directory=root();
        relative.record.link.association.objects[relative.record.additional_object_inputs.size()+i]=source.object;
    }
    const auto relative_model=archived_model(roundtrip({input(relative)}));
    check(!has(relative_model.model.records[0],Issue::unresolved_path) &&
        relative_model.model.references.records[0].stages[0].working_directory==root(),"relative source paths retain their own recorded cwd");
}

LinkFactSnapshot snapshot(const RecordedTargetResult& value) {
    LinkFactSnapshot snap;
    const auto& link=value.record.link;
    snap.capture_label="historical-snapshot-label";
    snap.completion=link.completion;snap.cache_state=link.cache_state;snap.linked=value.result.link.linked;
    snap.output=utf8(link.association.output);snap.cache_file=utf8(link.cache_file);
    snap.working_directory=utf8(*link.working_directory);snap.signature=link.association.signature.digest();
    snap.linker_path=utf8(link.association.linker.linker);snap.linker_version=link.association.linker.version;
    snap.linker_stamp=link.association.linker.binary_stamp;
    snap.target_kind=link.options.target_kind;
    return snap;
}
void ownership_contracts() {
    auto value=fixture();auto in=input(value);in.snapshot=snapshot(value);
    auto projected=roundtrip({in},{retain()});const auto bytes=encode(projected);
    check(projected.records[0].snapshot==in.snapshot,"nested existing snapshot is retained separately");
    check(bytes.find("PRIVATE-")==std::string::npos,"process output and environment never enter the archive");
    auto ignored=value;
    ignored.cache_evidence.compiles[0].inspection_toolchain.environment[0].value.assign(ArtifactGenerationArchiveLimits::text_bytes+1,'!');
    ignored.result.compiles[0].result.process->stdout_text.assign(1024*1024,'?');
    auto ignored_in=input(ignored);ignored_in.snapshot=in.snapshot;
    check(encode(project({ignored_in},{retain()}))==bytes,"unselected process/environment data neither changes bytes nor consumes the selected budget");
    value.record.compiler_options.defines[0]="changed";
    value.cache_evidence.compiles[0].cache_entry.signature=BuildSignature::from_digest({1,1});
    in.source_id="changed";in.snapshot->capture_label="changed";
    check(encode(projected)==bytes,"projected claims own source, snapshot, options and captured values");
    auto expired=[] {
        auto temporary=fixture();
        return project({input(temporary)},{retain()});
    }();
    const auto expected=encode(expired);
    auto decoded=[&] { auto temporary_bytes=expected;return take(decode_artifact_generation_archive(temporary_bytes,lexical_key),"temporary decode"); }();
    check(encode(decoded)==expected && archived_model(decoded).compiles[0][1].cache_toolchain.version=="execution-v1",
        "claims survive destruction of both live invocation and input byte string");
    auto mismatch=fixture();auto mismatch_in=input(mismatch);mismatch_in.snapshot=snapshot(mismatch);
    mismatch_in.snapshot->signature.high^=1;
    check(has(archived_model(roundtrip({mismatch_in})).model.records[0],Issue::snapshot_mismatch),"snapshot cannot override a captured terminal digest");
    auto lib=static_fixture();auto lib_in=input(lib);lib_in.snapshot=snapshot(mismatch);
    check(has(archived_model(roundtrip({lib_in})).model.records[0],Issue::snapshot_mismatch),"link snapshot does not certify a static archive");
}

void same_error(const RecordedArtifactGenerationError& original,const RecordedArtifactGenerationError& archived) {
    check(original.message==archived.message && original.record_index==archived.record_index &&
        original.projection_error.has_value()==archived.projection_error.has_value(),"record error and original diagnostic survive");
    if(original.projection_error)check(original.projection_error->issue==archived.projection_error->issue &&
        original.projection_error->source_index==archived.projection_error->source_index &&
        original.projection_error->message==archived.projection_error->message,"strict projector issue and original source slot survive");
}
void semantic_rejection_contracts() {
    const auto original=fixture(true);
    const auto base=project({input(original),input(original,"second-record","generation-002")});
    const auto reject=[&](auto edit,RecordedStorageProjectionIssue issue,std::optional<std::size_t> slot) {
        auto changed=original;edit(changed);
        auto live_inputs=std::vector{input(original),input(changed,"second-record","generation-002")};
        const auto live=model_recorded_artifact_generations(live_inputs,{},root(),lexical_key);
        check(!live && live.error().record_index==1 && live.error().projection_error &&
            live.error().projection_error->issue==issue && live.error().projection_error->source_index==slot,"negative control has the original typed error");
        const auto projected=project_artifact_generation_archive(live_inputs,{},root(),lexical_key);
        check(!projected && projected.error().model_error.has_value(),"live projection cannot archive invalid evidence");
        same_error(live.error(),*projected.error().model_error);
        auto archive=base;edit(std::get<ArchivedTargetClaim>(archive.records[1].record));
        const auto modeled=model_archived_artifact_generations(archive,lexical_key);
        check(!modeled,"edited claim cannot bypass the shared strict model");same_error(live.error(),modeled.error());
        const auto encoded=encode_artifact_generation_archive(archive,lexical_key);
        check(!encoded && encoded.error().model_error.has_value(),"encoder refuses invalid evidence as a whole");
        same_error(live.error(),*encoded.error().model_error);
    };
    using PI=RecordedStorageProjectionIssue;
    reject([](auto& r){r.cache_evidence.compiles.pop_back();},PI::source_count,{});
    reject([](auto& r){std::swap(r.cache_evidence.compiles[0],r.cache_evidence.compiles[1]);},PI::source_mismatch,0);
    reject([](auto& r){r.cache_evidence.compiles[1].cache_entry.source=root()/"other.cpp";},PI::cache_mismatch,1);
    reject([](auto& r){r.cache_evidence.compiles[1].request.options.defines={"different"};},PI::options_mismatch,1);
    reject([](auto& r){r.result.compiles[1].result.compiled=false;},PI::outcome_mismatch,1);
    reject([](auto& r){r.result.any_compiled=false;},PI::outcome_mismatch,{});
    reject([](auto& r){std::swap(r.record.link.association.objects[1],r.record.link.association.objects[2]);},PI::terminal_mismatch,{});
    reject([](auto& r){r.record.link.association.file_inputs.push_back(root()/"plugin.lib");},PI::path_conflict,{});
    reject([](auto& r){r.record.link.association.side_outputs.push_back(r.record.sources[0].source);},PI::path_conflict,{});
    reject([](auto& r){r.cache_evidence.compiles[1].request.module_scan_output=root()/"unit-1.scan.json";},PI::cache_mismatch,1);
    reject([](auto& r){r.cache_evidence.compiles[1].cache_entry.module_scan=ModuleScanEvidence{
        .signature=BuildSignature::from_digest({12,13}),.source={.path=root()/"unit-1.cpp",.exists=true},
        .output={.path=root()/"unit-1.scan.json",.exists=true},.dependencies={}};},PI::cache_mismatch,1);
    auto count=base;count.records.resize(ArtifactGenerationLimits::records+1,base.records[0]);
    unsigned keys=0;
    const auto key=[&](const fs::path& path){++keys;return lexical_key(path);};
    check(!encode_artifact_generation_archive(count,key) && keys==0,"record bound precedes semantic callbacks");
    auto text=base;text.records[1].source_id.assign(ArtifactGenerationArchiveLimits::string_bytes+1,'x');
    keys=0;check(!encode_artifact_generation_archive(text,key) && keys==0,"typed string bound precedes semantic callbacks");
    auto wrong_enum=base;
    auto& bad=std::get<ArchivedTargetClaim>(wrong_enum.records[1].record);
    bad.record.link.options.architecture=static_cast<Architecture>(255);
    keys=0;check(!encode_artifact_generation_archive(wrong_enum,key) && keys==0,"unknown typed enum is not serialized");
    wrong_enum=base;
    std::get<ArchivedTargetClaim>(wrong_enum.records[1].record).result.link.warnings[0].code=static_cast<IncrementalLinkWarningCode>(255);
    keys=0;check(!encode_artifact_generation_archive(wrong_enum,key) && keys==0,"unknown link warning enum is not serialized");
    wrong_enum=base;
    auto& compile_warning=std::get<ArchivedTargetClaim>(wrong_enum.records[1].record);
    compile_warning.record.sources[1].has_warnings=true;
    compile_warning.result.compiles[1].result.warnings.push_back({static_cast<IncrementalCompileWarningCode>(255),root()/"warning.cpp","warning"});
    keys=0;check(!encode_artifact_generation_archive(wrong_enum,key) && keys==0,"unknown compile warning enum is not serialized");
    wrong_enum=base;
    auto& save=std::get<ArchivedTargetClaim>(wrong_enum.records[1].record);
    save.record.sources[1].has_warnings=true;
    save.result.compiles[1].result.warnings.push_back({IncrementalCompileWarningCode::cache_save_failed,
        save.cache_evidence.compiles[1].request.cache_file,"failed"});
    save.cache_evidence.compiles[1].state=CompileCacheEvidenceState::save_failed;
    save.cache_evidence.compiles[1].save_error=CompileCacheFileError{.code=static_cast<CompileCacheFileErrorCode>(255),
        .file=save.cache_evidence.compiles[1].request.cache_file,.offset=0,.message="failed"};
    keys=0;check(!encode_artifact_generation_archive(wrong_enum,key) && keys==0,"unknown save error enum is not serialized");
    auto library=static_fixture();auto wrong_archive_warning=project({input(library)});
    std::get<ArchivedStaticTargetClaim>(wrong_archive_warning.records[0].record).result.archive.warnings[0].code=static_cast<IncrementalArchiveWarningCode>(255);
    keys=0;check(!encode_artifact_generation_archive(wrong_archive_warning,key) && keys==0,"unknown LIB warning enum is not serialized");
    check(!encode_artifact_generation_archive(base,{}),"missing path authority cannot encode claims");
    bool propagated=false;
    try {(void)encode_artifact_generation_archive(base,[](const fs::path&)->std::string{throw std::runtime_error("pure-key-sentinel");});}
    catch(const std::runtime_error& e){propagated=std::string_view(e.what())=="pure-key-sentinel";}
    check(propagated,"pure path callback exception propagates without partial output");
    // A syntactically valid hostile wire edit must receive the same original
    // semantic error, including its record slot; no string is treated as code.
    auto wire_source=original;
    const auto benign=root()/"benign.def",collision=root()/"plugin.lib";
    wire_source.record.link.association.file_inputs={benign};
    auto wire=encode(project({input(original),input(wire_source,"wire-second","generation-002")}));
    const auto needle=utf8(benign),replacement=utf8(collision);
    const auto where=wire.find(needle);
    check(needle.size()==replacement.size() && where!=std::string::npos && wire.find(needle,where+1)==std::string::npos,
        "semantic wire mutation locates one unique equal-length input path");
    wire.replace(where,needle.size(),replacement);
    wire_source.record.link.association.file_inputs={collision};
    const auto expected=model_recorded_artifact_generations(
        std::vector{input(original),input(wire_source,"wire-second","generation-002")},{},root(),lexical_key);
    const auto from_wire=decode_artifact_generation_archive(wire,lexical_key);
    check(!expected && !from_wire && from_wire.error().model_error &&
        expected.error().record_index==1 && expected.error().projection_error->issue==PI::path_conflict,
        "valid framing cannot hide a genuine DLL role conflict");
    same_error(expected.error(),*from_wire.error().model_error);
}

std::uint32_t u32(std::string_view bytes,std::size_t offset) {
    check(offset+4<=bytes.size(),"test locator has a complete length field");
    std::uint32_t result{};
    for(unsigned i=0;i<4;++i)result|=std::uint32_t(static_cast<unsigned char>(bytes[offset+i]))<<(i*8);
    return result;
}
void set_u32(std::string& bytes,std::size_t offset,std::uint32_t value) {
    check(offset+4<=bytes.size(),"test mutation is inside encoded buffer");
    for(unsigned i=0;i<4;++i)bytes[offset+i]=static_cast<char>((value>>(i*8))&255);
}
void skip_string(std::string_view bytes,std::size_t& offset) { offset+=4+u32(bytes,offset); }
void structural_refusal(std::string_view bytes,const char* message) {
    unsigned keys=0;
    const auto decoded=decode_artifact_generation_archive(bytes,[&](const fs::path& path){++keys;return lexical_key(path);});
    check(!decoded && keys==0,message);
    check(decoded.error().offset<=bytes.size(),"wire diagnostic offset is bounded by supplied bytes");
}
void malformed_wire_contracts() {
    const auto empty=encode(project({}));
    // Independent v1 golden: fixed magic/version/kind/authority, explicit root
    // bytes, zero record count and zero retention count. No production writer
    // helper computes this expectation.
    std::string golden{"MQBGARCH",8};golden.append("\x01\0\0\0\x01\0",6);
#ifdef _WIN32
    golden.append("\x15\0\0\0",4);golden+="C:/generation-archive";
#else
    golden.append("\x13\0\0\0",4);golden+="/generation-archive";
#endif
    golden.append(8,'\0');
    check(empty==golden,"hand-constructed empty v1 golden protects fixed framing");
    check(take(decode_artifact_generation_archive(golden,lexical_key),"golden decode").records.empty(),"independent golden decodes");
    const auto value=fixture();const auto populated=encode(project({input(value)}));
    check(populated.substr(0,8)=="MQBGARCH" && u32(populated,8)==1 && populated[12]==1 && populated[13]==0,
        "fixed schema magic, version, kind and false authority");
    check(u32(populated,14)==utf8(root()).size() && populated.substr(18,u32(populated,14))==utf8(root()),"length-delimited UTF-8 root");
    const auto records_offset=18+u32(populated,14);
    check(u32(populated,records_offset)==1,"record count is explicitly framed");
    std::size_t generation_flag=records_offset+4;
    for(unsigned i=0;i<3;++i)skip_string(populated,generation_flag);
    check(populated[generation_flag]==1,"fixture carries an explicit generation");
    std::size_t variant_offset=generation_flag+1;skip_string(populated,variant_offset);
    check(populated[variant_offset]==1,"LINK claim uses the documented variant tag");
    std::size_t compiler_enum_offset=variant_offset+1;
    check(populated[compiler_enum_offset++]==1,"fixture body contains a caller label");
    skip_string(populated,compiler_enum_offset);skip_string(populated,compiler_enum_offset);
    check(populated[compiler_enum_offset]==1 && populated[compiler_enum_offset+1]==2 && populated[compiler_enum_offset+2]==4,
        "explicit v1 debug/x64/cpp23 tags follow the framed caller label");
    const auto changed_byte=[&](std::size_t offset,unsigned char replacement,const char* message) {
        auto damaged=populated;damaged[offset]=static_cast<char>(replacement);structural_refusal(damaged,message);
    };
    changed_byte(0,'X',"wrong magic refused before key");
    auto future=populated;set_u32(future,8,2);
    auto future_result=decode_artifact_generation_archive(future,lexical_key);
    check(!future_result && future_result.error().code==Error::unsupported_version,"future version is explicit");
    structural_refusal(future,"future version never reaches semantic callbacks");
    changed_byte(12,2,"unknown document kind refused");
    changed_byte(13,1,"authority escalation refused");
    changed_byte(generation_flag,2,"non-boolean optional flag refused");
    changed_byte(variant_offset,3,"unsupported module/unknown variant refused");
    changed_byte(compiler_enum_offset,255,"unknown nested compiler configuration enum refused");
    changed_byte(compiler_enum_offset+1,255,"unknown nested compiler architecture enum refused");
    changed_byte(compiler_enum_offset+2,255,"unknown nested compiler standard enum refused");
    changed_byte(18,0,"NUL in root text refused");
    changed_byte(18,255,"invalid UTF-8 leading byte refused");
    changed_byte(records_offset+8,0,"NUL in a nested record identifier refused");
    changed_byte(records_offset+8,255,"invalid UTF-8 in a nested record identifier refused");
    for(const auto& invalid:std::vector<std::string>{std::string("\xc0\xaf",2),std::string("\xed\xa0\x80",3),std::string("\xf4\x90\x80\x80",4)}) {
        auto damaged=populated;damaged.replace(18,invalid.size(),invalid);structural_refusal(damaged,"noncanonical UTF-8 refused");
    }
    auto length=populated;set_u32(length,14,std::numeric_limits<std::uint32_t>::max());
    structural_refusal(length,"hostile root length refused before allocation");
    length=populated;set_u32(length,records_offset+4,std::numeric_limits<std::uint32_t>::max());
    structural_refusal(length,"hostile record string length refused before allocation");
    auto count=populated;set_u32(count,records_offset,ArtifactGenerationLimits::records+1);
    structural_refusal(count,"record count over limit refused before allocation");
    count=populated;set_u32(count,records_offset,std::numeric_limits<std::uint32_t>::max());
    structural_refusal(count,"overflowing record count refused");
    auto retained=populated;set_u32(retained,retained.size()-4,std::numeric_limits<std::uint32_t>::max());
    structural_refusal(retained,"hostile trailing retention count refused");
    auto trailing=populated;trailing+='\0';structural_refusal(trailing,"trailing data refused before semantic callbacks");
    for(std::size_t n=0;n<empty.size();++n)structural_refusal(std::string_view(empty).substr(0,n),"every truncated empty document is refused");
    for(std::size_t n=0;n<populated.size();++n)structural_refusal(std::string_view(populated).substr(0,n),"every truncated populated document is refused");
    const std::string oversized(ArtifactGenerationArchiveLimits::document_bytes+1,'x');
    structural_refusal(oversized,"document cap checked before interpretation");
    auto cumulative_input=input(value);cumulative_input.source_id.assign(64*1024,'s');
    const auto one_large_record=encode(project({cumulative_input}));
    const std::string_view body{one_large_record.data()+records_offset+4,one_large_record.size()-records_offset-8};
    auto cumulative=one_large_record.substr(0,records_offset+4);set_u32(cumulative,records_offset,130);
    for(unsigned i=0;i<130;++i)cumulative.append(body);
    cumulative.append(4,'\0');
    check(cumulative.size()<ArtifactGenerationArchiveLimits::document_bytes,"cumulative budget case fits the outer document cap");
    structural_refusal(cumulative,"individually bounded records still share cumulative budgets");
    auto snapshot_in=input(value);snapshot_in.snapshot=snapshot(value);
    auto nested=encode(project({snapshot_in}));
    const auto marker=nested.find("\"schema\":\"mqb.link-fact-snapshot\",\"version\":1");
    check(marker!=std::string::npos,"snapshot remains the existing bounded schema");
    const auto version=nested.find("\"version\":1",marker)+10;
    check(nested[version]=='1',"nested snapshot version locator");nested[version]='9';
    structural_refusal(nested,"unsupported nested snapshot version refused before path callbacks");
    unsigned callbacks=0;
    auto valid=decode_artifact_generation_archive(populated,[&](const fs::path& path){++callbacks;return lexical_key(path);});
    check(valid.has_value() && callbacks>0,"valid wire still traverses the shared semantic validator");
    check(!decode_artifact_generation_archive(populated,{}),"decoder also requires explicit path authority");
    bool propagated=false;
    try {(void)decode_artifact_generation_archive(populated,[](const fs::path&)->std::string{throw std::runtime_error("decode-key-sentinel");});}
    catch(const std::runtime_error& e){propagated=std::string_view(e.what())=="decode-key-sentinel";}
    check(propagated,"decoder does not disguise an exceptional path authority as successful evidence");
}
void allocation_preflight_contracts() {
    const auto value=fixture();auto large_input=input(value);
    constexpr std::size_t large_string_bytes=256*1024;
    constexpr std::size_t vector_entries=2048;
    std::size_t control_size{},largest{};
    {
        archive_allocation_probe::Scope probe;
        const std::string control(large_string_bytes,'c');control_size=control.size();
        largest=archive_allocation_probe::largest_request;
    }
    check(control_size==large_string_bytes && largest>=large_string_bytes,"allocation observer detects a real owning string");
    {
        archive_allocation_probe::Scope probe;
        const std::vector<std::string> control(vector_entries);control_size=control.size();
        largest=archive_allocation_probe::largest_request;
    }
    check(control_size==vector_entries && largest>=vector_entries*sizeof(std::string),"allocation observer detects a real owning vector");
    large_input.source_id.assign(large_string_bytes,'s');
    auto text_wire=encode(project({large_input}));text_wire+='!';
    unsigned keys=0;bool refused=false;
    {
        archive_allocation_probe::Scope probe;
        const auto decoded=decode_artifact_generation_archive(text_wire,[&](const fs::path& path){++keys;return lexical_key(path);});
        refused=!decoded;largest=archive_allocation_probe::largest_request;
    }
    check(refused && keys==0 && largest<large_string_bytes,
        "late malformed data is rejected before allocating an owning large document string");
    auto vector_value=fixture();
    vector_value.record.compiler_options.defines.assign(vector_entries,std::string{});
    for(auto& compile:vector_value.cache_evidence.compiles)compile.request.options=vector_value.record.compiler_options;
    auto vector_wire=encode(project({input(vector_value)}));vector_wire+='!';
    keys=0;refused=false;
    {
        archive_allocation_probe::Scope probe;
        const auto decoded=decode_artifact_generation_archive(vector_wire,[&](const fs::path& path){++keys;return lexical_key(path);});
        refused=!decoded;largest=archive_allocation_probe::largest_request;
    }
    check(refused && keys==0 && largest<vector_entries*sizeof(std::string),
        "late malformed data is rejected before allocating an owning document vector");
}
void run() {
    roundtrip_contracts();ownership_contracts();semantic_rejection_contracts();malformed_wire_contracts();allocation_preflight_contracts();
    std::cout << checks << " artifact generation archive checks passed\n";
}
} // namespace

int main() {
    try {run();return 0;}
    catch(const std::exception& error) {
        std::cerr << "FAIL at archive check " << checks << ": " << error.what() << '\n';
        return 1;
    }
}
#endif
