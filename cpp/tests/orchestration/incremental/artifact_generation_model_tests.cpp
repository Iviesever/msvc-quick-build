#include "mqb/orchestration/ArtifactGenerationModel.hpp"
#include "mqb/orchestration/MsvcIncrementalStaticTargetCoordinator.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using namespace mqb;
using namespace mqb::orchestration;
using State = ArtifactGenerationState;
using Issue = ArtifactGenerationIssue;
namespace fs = std::filesystem;
unsigned checks{};
void check(bool ok, const char* text) { ++checks; if (!ok) throw std::runtime_error(text); }
fs::path root() {
#ifdef _WIN32
    return "C:/generation-model";
#else
    return "/generation-model";
#endif
}
auto lexical_key = [](const fs::path& p) { return p.generic_string(); };
LinkArtifactRecord link_record(ArtifactCompletion c) {
    return {.completion=c, .cache_state=c == ArtifactCompletion::executed ? ArtifactCacheState::saved : ArtifactCacheState::reused,
        .association={.linker={"link.exe", "v1", "stamp"}, .signature=BuildSignature::from_digest({3,4}),
            .objects={root()/"a.obj"}, .output=root()/"app.exe", .libraries={}, .file_inputs={}, .side_outputs={}},
        .options={}, .cache_file=root()/"app.linkcache", .working_directory=root()};
}
ArtifactGenerationInput input(std::string source, std::string gen="g1", ArtifactCompletion c=ArtifactCompletion::executed) {
    TargetArtifactRecord record{.caller_label=ArtifactGenerationLabel{"annotation", "not-g1"}, .compiler_options={},
        .sources={{root()/"a.cpp", root()/"a.obj", root()/"a.d", root()/"a.cache", c, false}},
        .additional_object_inputs={}, .link=link_record(c)};
    return {std::move(source), {"project", "app"}, std::move(gen), ToolchainIdentity{"cl.exe", "v1", "stamp"},
        std::move(record), std::nullopt};
}
TargetArtifactRecord& target(ArtifactGenerationInput& i) { return std::get<TargetArtifactRecord>(i.record); }
ArtifactGenerationKey retain(std::string gen="g1") { return {{"project", "app"}, std::move(gen)}; }
ArtifactGenerationModel model(const std::vector<ArtifactGenerationInput>& inputs,
                              const std::vector<ArtifactGenerationKey>& retained={}) {
    auto out=model_artifact_generations(inputs,retained,root(),lexical_key);
    if (!out) throw std::runtime_error(out.error().message);
    return std::move(*out);
}
bool has(const ArtifactGenerationRecord& r, Issue i) { return std::find(r.issues.begin(),r.issues.end(),i)!=r.issues.end(); }
void run() {
    static_assert(!ArtifactGenerationModel::deletion_authorized && !ArtifactGenerationModel::producer_identity_verified &&
        !ArtifactGenerationModel::complete_producer_inventory && !ArtifactGenerationModel::current_content_verified);
    auto a=input("record-A"); auto b=input("record-B","g1",ArtifactCompletion::reused);
    auto out=model({a,b},{retain()});
    check(out.records[0].state==State::executed_claim && out.records[1].state==State::explicit_reuse,"explicit reuse matches sole origin");
    check(out.records[0].origin_record==0 && out.records[1].origin_record==0,"reuse creates no second origin");
    check(out.retention[0].state==ArtifactRetentionState::selected && out.retention[0].associated_records==std::vector<std::size_t>{0,1},"retention follows explicit reuse");
    check(out.records[0].caller_label->generation=="not-g1" && out.records[0].generation=="g1","label remains separate");
    check(out.references.records.size()==2 && out.references.observation.entries.empty(),"typed projections retained without observation");
    const auto reverse=model({b,a},{retain()});
    check(reverse.records[0].origin_record==1 && reverse.records[1].origin_record==1,"input ordering does not invent temporal ordering");
    target(b).caller_label=ArtifactGenerationLabel{"different label","different generation"};
    check(model({a,b}).records[1].state==State::explicit_reuse,"annotations are not identity");
    auto missing=b; missing.generation.reset();
    check(has(model({missing}).records[0],Issue::missing_generation),"missing generation not inferred from labels/signature");
    check(has(model({b}).records[0],Issue::missing_origin),"reuse without origin stays unresolved");
    missing=b; missing.compiler.reset();
    check(has(model({a,missing}).records[1],Issue::missing_compiler),"missing compiler evidence is not inferred");
    missing=a; missing.compiler->binary_stamp.clear();
    check(!model({missing}).records[0].origin_record,"incomplete compiler stamp cannot resolve identity");
    auto duplicate=a; duplicate.source_id="other-record";
    out=model({a,duplicate,b},{retain()});
    check(has(out.records[0],Issue::duplicate_generation) && has(out.records[1],Issue::duplicate_generation),"duplicate generation reports all candidates");
    check(has(out.records[2],Issue::ambiguous_origin) && out.retention[0].state==ArtifactRetentionState::ambiguous,"duplicate execution never chooses a winner");
    duplicate=input("record-A","g2");
    check(has(model({a,duplicate}).records[0],Issue::duplicate_source),"duplicate provenance not deduplicated");
    auto other=input("new-generation","g2"); out=model({a,other},{retain("g1"),retain("absent")});
    check(out.records[1].origin_record==1 && out.retention[1].state==ArtifactRetentionState::missing,"new explicit generation stays distinct; absent retention explicit");
    bool collision=false;
    for (const auto& p:out.paths) if (p.path_key==lexical_key(root()/"app.exe")) {
        collision=p.multiple_executed_generations && p.retained_records==std::vector<std::size_t>{0};
    }
    check(collision,"same output across generations is a potential overwrite, unselected not retired");
    out=model({b},{retain()});
    check(out.retention[0].state==ArtifactRetentionState::unresolved,"retaining missing producer never manufactures it");
    check(model({a},{retain(),retain()}).retention.size()==2,"duplicate explicit requests remain visible");

    // Every captured effective option participates, including ordering and duplicates.
    const auto mismatch=[&](auto edit) {
        auto changed=input("reuse","g1",ArtifactCompletion::reused); edit(changed);
        check(has(model({a,changed}).records[1],Issue::recipe_mismatch),"changed effective recipe cannot attach to generation");
    };
    mismatch([](auto& x){x.compiler->compiler="other-cl.exe";});
    mismatch([](auto& x){x.compiler->version="v2";});
    mismatch([](auto& x){x.compiler->binary_stamp="other";});
    mismatch([](auto& x){target(x).compiler_options.configuration=BuildConfiguration::release;target(x).link.options.configuration=BuildConfiguration::release;});
    mismatch([](auto& x){target(x).compiler_options.architecture=Architecture::x86;target(x).link.options.architecture=Architecture::x86;});
    mismatch([](auto& x){target(x).compiler_options.standard=CppStandard::cpp20;});
    mismatch([](auto& x){target(x).compiler_options.runtime_library=RuntimeLibrary::mt;});
    mismatch([](auto& x){target(x).compiler_options.link_time_code_generation=true;});
    mismatch([](auto& x){target(x).compiler_options.defines={"A=1","A=2"};});
    mismatch([](auto& x){target(x).compiler_options.include_directories={"inc"};});
    mismatch([](auto& x){target(x).compiler_options.additional_arguments={"/O1"};});
    mismatch([](auto& x){target(x).compiler_options.external_module_providers={{"M",root()/"m.ifc"}};});
    mismatch([](auto& x){target(x).compiler_options.precompiled_header=PrecompiledHeaderBinding{"pch.h",root()/"a.pch",PrecompiledHeaderRole::use};});
    mismatch([](auto& x){target(x).link.options.target_kind=TargetKind::dynamic_library;});
    mismatch([](auto& x){target(x).link.options.subsystem=LinkSubsystem::windows;});
    mismatch([](auto& x){target(x).link.options.link_time_code_generation=true;});
    mismatch([](auto& x){target(x).link.options.address_sanitizer_runtime_library=RuntimeLibrary::mt;});
    mismatch([](auto& x){target(x).link.options.address_sanitizer_vcasan_runtime_library=RuntimeLibrary::mt;});
    mismatch([](auto& x){target(x).link.options.fuzzer_runtime_library=RuntimeLibrary::mt;});
    mismatch([](auto& x){target(x).link.options.msvc_openmp_runtime=true;});
    mismatch([](auto& x){target(x).link.options.library_directories={"lib"};});
    mismatch([](auto& x){target(x).link.options.libraries={"other.lib"};});
    mismatch([](auto& x){target(x).link.options.additional_arguments={"/DEBUG"};});
    mismatch([](auto& x){target(x).link.association.linker.version="v2";});
    mismatch([](auto& x){target(x).link.association.signature=BuildSignature::from_digest({99,99});});
    mismatch([](auto& x){target(x).link.association.output=root()/"other.exe";});
    mismatch([](auto& x){target(x).link.association.file_inputs={"exports.def"};});
    mismatch([](auto& x){target(x).link.association.libraries={"resolved.lib"};});
    mismatch([](auto& x){target(x).sources[0].source="other.cpp";});
    auto ordered=a; target(ordered).compiler_options.additional_arguments={"/O1","/O2"};
    auto reordered=b; target(reordered).compiler_options.additional_arguments={"/O2","/O1"};
    check(has(model({ordered,reordered}).records[1],Issue::recipe_mismatch),"ordered raw options are not sorted");
    target(reordered).compiler_options.additional_arguments={"/O1","/O2","/O2"};
    check(has(model({ordered,reordered}).records[1],Issue::recipe_mismatch),"duplicate options are not discarded");
    auto mixed=b; target(mixed).sources[0].completion=ArtifactCompletion::executed;
    check(has(model({a,mixed}).records[1],Issue::mixed_reuse),"partial execution cannot be a whole-target reuse claim");
    auto invalid=a;target(invalid).link.cache_state=ArtifactCacheState::reused;
    check(has(model({invalid}).records[0],Issue::invalid_completion),"invalid completion/cache pair refused");
    invalid=a;target(invalid).link.options.architecture=static_cast<Architecture>(42);
    check(has(model({invalid}).records[0],Issue::invalid_recipe),"unknown recipe enum remains conflicting");
    invalid=a;target(invalid).compiler_options.configuration=BuildConfiguration::release;
    check(has(model({invalid}).records[0],Issue::invalid_recipe),"compiler and terminal configuration inconsistency visible");

    auto release=input("release","g2");target(release).compiler_options.configuration=BuildConfiguration::release;
    target(release).link.options.configuration=BuildConfiguration::release;
    out=model({a,release},{retain()}); collision=false;
    for(const auto& p:out.paths) if(p.path_key==lexical_key(root()/"app.exe")) collision=p.different_target_or_recipe;
    check(collision,"Debug/Release same output visible without inferring present owner");
    auto consumer=input("consumer","g3");consumer.target.target="consumer";
    target(consumer).link.association.output=root()/"consumer.exe";
    target(a).compiler_options.precompiled_header=PrecompiledHeaderBinding{"pch.h",root()/"shared.pch",PrecompiledHeaderRole::use};
    target(consumer).compiler_options.precompiled_header=target(a).compiler_options.precompiled_header;
    out=model({a,consumer},{retain()});bool shared_object=false,shared_pch=false;
    for(const auto& p:out.paths){
        if(p.path_key==lexical_key(root()/"a.obj")) shared_object=p.shared_reference && p.records.size()==2;
        if(p.path_key==lexical_key(root()/"shared.pch")) shared_pch=p.shared_reference && p.retained_records==std::vector<std::size_t>{0};
    }
    check(shared_object && shared_pch,"shared object and PCH references do not become separately reclaimable bytes");

    // Module records use original provider/compile projections, not inferred IFC pairs.
    ModuleCompileArtifactRecord unit{.completion=ArtifactCompletion::executed,.cache_state=ArtifactCacheState::saved,
        .unit={.source=root()/"m.cppm",.kind=TranslationUnitKind::module_interface,.header_unit=std::nullopt,
            .dependencies={},.module_references={{"external",root()/"external.ifc"}},.header_unit_references={},
            .outputs={{root()/"m.ifc",ArtifactKind::module_interface}}},
        .compiler_options={},.dependencies=root()/"m.d",.compile_cache=root()/"m.cache",.module_scan_output=std::nullopt,
        .working_directory=root(),.force_rebuild=false};
    ModuleTargetArtifactRecord module{.caller_label=std::nullopt,.scans={},
        .compiles={.caller_label=std::nullopt,.dependencies={},.compiles={unit},.header_unit_compiles={}},.link=link_record(ArtifactCompletion::executed)};
    auto mi=input("module");mi.record=module;
    auto mc=mi;mc.source_id="other-module";mc.target.target="other-module";
    out=model({mi,mc},{retain()});bool shared_ifc=false;
    for(const auto& p:out.paths) if(p.path_key==lexical_key(root()/"external.ifc")) shared_ifc=p.shared_reference;
    check(shared_ifc,"shared IFC consumption comes from typed module references");
    check(std::none_of(out.references.records[0].stages[0].paths.begin(),out.references.records[0].stages[0].paths.end(),
        [](const auto& p){return p.role==ArtifactPathRole::declared_output && p.path.extension()==".obj";}),"IFC-only producer has no invented object");
    auto mr=mi;mr.source_id="module-reuse";
    auto& m=std::get<ModuleTargetArtifactRecord>(mr.record);m.link.completion=ArtifactCompletion::reused;m.link.cache_state=ArtifactCacheState::reused;
    m.compiles.compiles[0].completion=ArtifactCompletion::reused;m.compiles.compiles[0].cache_state=ArtifactCacheState::reused;
    check(model({mi,mr}).records[1].state==State::explicit_reuse,"explicit module reuse uses captured unit recipe");
    m.compiles.compiles[0].unit.module_references[0].logical_name="different-provider";
    check(has(model({mi,mr}).records[1],Issue::recipe_mismatch),"same IFC path with different provider name not silently equal");

    StaticTargetArtifactRecord st{.caller_label=std::nullopt,.compiler_options={},.sources={},.additional_object_inputs={},
        .archive={.completion=ArtifactCompletion::executed,.cache_state=ArtifactCacheState::saved,
            .association={.librarian={"lib.exe","v1","stamp"},.signature=BuildSignature::from_digest({7,8}),.objects={root()/"a.obj"},.output=root()/"a.lib"},
            .architecture=Architecture::x64,.link_time_code_generation=false,.additional_arguments={},.cache_file=root()/"a.archivecache",.working_directory=root()}};
    auto sa=input("static");sa.record=st;auto sr=sa;sr.source_id="static-reuse";
    auto& archive=std::get<StaticTargetArtifactRecord>(sr.record).archive;archive.completion=ArtifactCompletion::reused;archive.cache_state=ArtifactCacheState::reused;
    check(model({sa,sr}).records[1].state==State::explicit_reuse,"static target retains compiler configuration plus actual LIB recipe");
    archive.additional_arguments={"/LTCG"};check(has(model({sa,sr}).records[1],Issue::recipe_mismatch),"static ordered options participate");

    auto snapshot_input=input("snapshot");const auto& l=target(snapshot_input).link;
    auto path_text=[](const fs::path& p){auto u=p.u8string();return std::string{reinterpret_cast<const char*>(u.data()),u.size()};};
    LinkFactSnapshot s; s.completion=ArtifactCompletion::executed;s.cache_state=ArtifactCacheState::saved;s.linked=true;
    s.output=path_text(l.association.output);s.cache_file=path_text(l.cache_file);s.working_directory=path_text(*l.working_directory);
    s.signature=l.association.signature.digest();s.linker_path="link.exe";s.linker_version="v1";s.linker_stamp="stamp";
    snapshot_input.snapshot=s;out=model({snapshot_input});
    check(out.records[0].state==State::executed_claim && out.records[0].snapshot==s,"optional snapshot is corroborating history not full inventory");
    snapshot_input.snapshot->signature.high=100;
    check(has(model({snapshot_input}).records[0],Issue::snapshot_mismatch),"mismatched historical snapshot cannot validate generation");
    sa.snapshot=s;check(has(model({sa}).records[0],Issue::snapshot_mismatch),"link snapshot cannot represent a LIB target");
    snapshot_input=input("no-identity");snapshot_input.generation.reset();snapshot_input.compiler.reset();snapshot_input.snapshot=s;
    check(!model({snapshot_input}).records[0].origin_record,"single legal snapshot alone never becomes a generation inventory");

    auto relative=input("relative");target(relative).sources[0].object="a.obj";
    out=model({relative},{retain()});
    check(has(out.records[0],Issue::unresolved_path),"source lacking its own cwd is not resolved with link cwd");
    check(std::any_of(out.references.matches.begin(),out.references.matches.end(),[](const auto& x){return !x.resolved_path;}),"unresolved paths retained");
    auto traversal=input("parent");target(traversal).link.association.output="../escape.exe";
    check(has(model({traversal}).records[0],Issue::unresolved_path),"parent traversal never probed or normalized through aliases");
    auto foreign=input("foreign");foreign.target.project="another-project";
    check(model({foreign,b}).records[1].state==State::unresolved,"generation key is scoped to explicit project and target");

    auto invalid_request=input("bad");invalid_request.target.target.clear();
    check(!model_artifact_generations(std::vector{invalid_request},{},root(),lexical_key),"empty target rejected before partial output");
    invalid_request=input(std::string("a\0b",3));
    check(!model_artifact_generations(std::vector{invalid_request},{},root(),lexical_key),"NUL provenance rejected");
    invalid_request=input("large");invalid_request.source_id.assign(ArtifactGenerationLimits::text_bytes+1,'x');
    check(!model_artifact_generations(std::vector{invalid_request},{},root(),lexical_key),"oversize text refused");
    std::vector<ArtifactGenerationInput> too_many(ArtifactGenerationLimits::records+1,input("bounded"));
    check(!model_artifact_generations(too_many,{},root(),lexical_key),"record bound enforced");
    check(!model_artifact_generations(std::vector{input("key")},{},root(),{}),"missing path key refused");
    check(!model_artifact_generations(std::vector{input("key")},{},root(),[](const auto&){return std::string{};}),"empty callback key refused");
    bool propagated=false;
    try {(void)model_artifact_generations(std::vector{input("throw")},{},root(),[](const auto&)->std::string{throw std::runtime_error("callback");});}
    catch(const std::runtime_error& e){propagated=std::string_view(e.what())=="callback";}
    check(propagated,"key exception propagates without partial success");
    check(model({}).records.empty(),"empty input never creates generations");
    a=input("owned-copy");out=model({a},{retain()});target(a).link.association.output="mutated";a.source_id="mutated";
    check(out.records[0].source_id=="owned-copy" && out.references.records[0].stages.back().paths[1].path==root()/"app.exe","results own their historical source and paths");
}
}

// BEGIN recorded generation model contracts
namespace recorded_generation_contracts {
unsigned count{};
void expect(bool ok, const char* message) {
    ++count; if (!ok) throw std::runtime_error(message);
}
RecordedTargetResult fixture(bool dll = false) {
    RecordedTargetResult r{.record={.link=link_record(ArtifactCompletion::executed)}};
    r.record.caller_label = ArtifactGenerationLabel{"annotation", "not-an-origin"};
    r.record.link = link_record(ArtifactCompletion::executed);
    r.record.link.association.objects.clear();
    r.record.link.options.target_kind = dll ? TargetKind::dynamic_library : TargetKind::executable;
    if (dll) {
        r.record.link.association.output = root()/"plugin.dll";
        r.record.link.association.side_outputs = {root()/"plugin.lib", root()/"plugin.exp"};
    }
    r.result.link.linked = true; r.result.any_compiled = true;
    r.record.compiler_options.defines = {"ONE=1", "TWO=2"};
    for (unsigned i = 0; i < 2; ++i) {
        const auto name = std::to_string(i);
        const auto source = root()/(name+".cpp"), object = root()/(name+".obj");
        const auto deps = root()/(name+".json"), cache = root()/(name+".cache");
        r.record.sources.push_back({source, object, deps, cache, ArtifactCompletion::executed, false});
        TargetCompileResult result{.source=source}; result.result.compiled = true;
        r.result.compiles.push_back(std::move(result));
        ToolchainIdentity compiler{root()/"cl.exe", "v1", "compiler-"+name};
        r.cache_evidence.compiles.push_back({
            .request={.unit={.source=source, .outputs={{object,ArtifactKind::object}}},
                .options=r.record.compiler_options, .cache_file=cache, .source_dependencies_file=deps,
                .working_directory=root()/("cwd-"+name)},
            .inspection_toolchain={.identity=compiler, .environment={{"PRIVATE","do-not-copy-environment"}}},
            .cache_entry={.source=source, .toolchain=compiler, .signature=BuildSignature::from_digest({11,i}),
                .outputs={{object,ArtifactKind::object}}, .dependencies={root()/"shared.hpp"},
                .include_search_roots={root()/"include"}},
            .state=CompileCacheEvidenceState::saved});
        r.record.link.association.objects.push_back(object);
    }
    return r;
}
void reuse_source(RecordedTargetResult& r, std::size_t i) {
    r.record.sources[i].completion = ArtifactCompletion::reused;
    r.result.compiles[i].result.compiled = false;
    r.cache_evidence.compiles[i].state = CompileCacheEvidenceState::reused;
}
void reuse(RecordedTargetResult& r) {
    for (std::size_t i=0; i<r.record.sources.size(); ++i) reuse_source(r,i);
    r.result.any_compiled = false; r.result.link.linked = false;
    r.record.link.completion = ArtifactCompletion::reused; r.record.link.cache_state = ArtifactCacheState::reused;
}
RecordedStaticTargetResult static_fixture() {
    const auto r = fixture();
    RecordedStaticTargetResult s{.record={.archive={.association={.signature=BuildSignature::from_digest({0,0})}}}};
    s.record.compiler_options = r.record.compiler_options; s.record.sources = r.record.sources;
    s.cache_evidence = r.cache_evidence;
    s.result.compiles = r.result.compiles; s.result.any_compiled = true; s.result.archive.archived = true;
    s.record.archive = {.completion=ArtifactCompletion::executed, .cache_state=ArtifactCacheState::saved,
        .association={.librarian={root()/"lib.exe","v1","stamp"}, .signature=BuildSignature::from_digest({3,4}),
            .objects=r.record.link.association.objects, .output=root()/"output.lib"},
        .architecture=Architecture::x64, .cache_file=root()/"output.archivecache", .working_directory=root()};
    return s;
}
template<class T>
RecordedArtifactGenerationInput in(const T& r, std::string id="origin", std::string gen="g1") {
    return {std::move(id), {"project","app"}, std::move(gen), std::cref(r), std::nullopt};
}
auto model(std::vector<RecordedArtifactGenerationInput> records, std::vector<ArtifactGenerationKey> retains = {}) {
    auto result = model_recorded_artifact_generations(records, retains, root(), lexical_key);
    if (!result) throw std::runtime_error("rich model: "+result.error().message);
    return std::move(*result);
}
void run() {
    static_assert(!RecordedArtifactGenerationModel::producer_identity_verified &&
                  !RecordedArtifactGenerationModel::current_content_verified &&
                  !RecordedArtifactGenerationModel::complete_producer_inventory &&
                  !RecordedArtifactGenerationModel::deletion_authorized);
    auto a=fixture(), b=a; reuse(b);
    auto result=model({in(a),in(b,"reuse")},{retain()});
    expect(result.model.records[0].state==State::executed_claim && result.model.records[1].state==State::explicit_reuse,
           "rich cold/warm have a single explicit origin");
    expect(result.model.retention[0].state==ArtifactRetentionState::selected &&
           result.model.retention[0].associated_records==std::vector<std::size_t>{0,1}, "rich retention follows origin");
    expect(result.compiles.size()==2 && result.compiles[0].size()==2, "context indices match records and sources");
    expect(result.compiles[0][0].inspection_toolchain.binary_stamp=="compiler-0" &&
           result.compiles[0][1].inspection_toolchain.binary_stamp=="compiler-1", "per-source identities not collapsed");
    expect(result.model.references.records[0].stages[0].working_directory==root()/"cwd-0" &&
           result.model.references.records[0].stages[1].working_directory==root()/"cwd-1", "each compile retains its own cwd");
    expect(result.model.references.records[1].stages[0].cache_state==ArtifactCacheState::reused, "warm is not saved");
    expect(result.model.records[0].recipe_evidence.find("do-not-copy-environment")==std::string::npos, "environment excluded");
    expect(result.model.records[0].caller_label->generation=="not-an-origin", "annotation not inferred as generation");
    expect(result.model.references.observation.entries.empty(), "no filesystem observation");
    expect(model({in(b,"reuse"),in(a)}).model.records[0].origin_record==1, "reverse input order retains explicit origin");
    expect(has(model({in(b)}).model.records[0],Issue::missing_origin), "reuse-only history does not manufacture origin");
    auto no_gen=in(a); no_gen.generation.reset();
    expect(has(model({no_gen}).model.records[0],Issue::missing_generation), "generation must be explicit");
    auto duplicated=in(a,"other");
    auto duplicate=model({in(a),duplicated,in(b,"reuse")},{retain()});
    expect(duplicate.model.retention[0].state==ArtifactRetentionState::ambiguous &&
           has(duplicate.model.records[2],Issue::ambiguous_origin), "duplicate producer claims are never deduplicated");
    expect(has(model({in(a),in(a)}).model.records[0],Issue::duplicate_source), "duplicate historical source IDs retained");
    expect(model({in(a)},{retain("absent")}).model.retention[0].state==ArtifactRetentionState::missing, "absent is not retired");
    const auto differing=[&](auto edit) {
        auto other=b; edit(other);
        expect(has(model({in(a),in(other,"reuse")}).model.records[1],Issue::recipe_mismatch), "selected rich field mismatch");
    };
    differing([](auto& r){r.cache_evidence.compiles[1].inspection_toolchain.identity.version="v2";});
    differing([](auto& r){r.cache_evidence.compiles[1].cache_entry.toolchain.version="executor-v2";});
    differing([](auto& r){r.cache_evidence.compiles[1].cache_entry.signature=BuildSignature::from_digest({9,9});});
    differing([](auto& r){r.cache_evidence.compiles[1].request.working_directory=root()/"other-cwd";});
    differing([](auto& r){r.cache_evidence.compiles[1].cache_entry.dependencies.push_back(root()/"another.hpp");});
    differing([](auto& r){r.cache_evidence.compiles[1].cache_entry.include_search_roots.push_back(root()/"other-include");});
    differing([](auto& r){
        std::swap(r.record.compiler_options.defines[0],r.record.compiler_options.defines[1]);
        for(auto& e:r.cache_evidence.compiles)e.request.options=r.record.compiler_options;
    });
    auto different=a; different.cache_evidence.compiles[0].cache_entry.toolchain.version="original executor";
    auto distinct=model({in(different)});
    expect(distinct.compiles[0][0].toolchains_differ &&
           distinct.compiles[0][0].cache_toolchain.version=="original executor", "inspection/cache difference retained");
    auto missing=a; missing.cache_evidence.compiles[1].inspection_toolchain.identity.binary_stamp.clear();
    expect(has(model({in(missing)}).model.records[0],Issue::missing_compiler), "incomplete inspection compiler stays unknown");
    missing=a; missing.cache_evidence.compiles[1].cache_entry.toolchain.compiler.clear();
    expect(has(model({in(missing)}).model.records[0],Issue::missing_compiler), "incomplete cache compiler stays unknown");
    auto partial=a; reuse_source(partial,0);
    auto p=model({in(partial)});
    expect(p.model.records[0].state==State::executed_claim &&
           p.model.references.records[0].stages[0].completion==ArtifactCompletion::reused, "partial source reuse remains visible");
    partial.record.link.completion=ArtifactCompletion::reused; partial.record.link.cache_state=ArtifactCacheState::reused;
    partial.result.link.linked=false;
    expect(has(model({in(partial)}).model.records[0],Issue::mixed_reuse), "executed source inside reused target conflicts");
    auto failed=a;
    auto& e=failed.cache_evidence.compiles[0];
    e.state=CompileCacheEvidenceState::save_failed;
    e.save_error=CompileCacheFileError{.code=CompileCacheFileErrorCode::replace_failed,.file=e.request.cache_file,.message="locked"};
    failed.result.compiles[0].result.warnings.push_back({IncrementalCompileWarningCode::cache_save_failed,e.request.cache_file,"locked"});
    failed.record.sources[0].has_warnings=true;
    auto failure=model({in(failed),in(b,"reuse")});
    expect(failure.compiles[0][0].save_error && failure.compiles[0][0].warnings[0].message=="locked" &&
           failure.model.references.records[0].stages[0].cache_state==ArtifactCacheState::save_failed, "failed save retained independently");
    expect(failure.model.records[1].state==State::explicit_reuse, "save outcome does not alter recipe identity");
    auto forced=a; forced.cache_evidence.compiles[0].request.force_rebuild=true;
    expect(model({in(forced),in(b,"reuse")}).compiles[0][0].force_rebuild, "force metadata retained but not identity");
    auto dll=fixture(true);
    expect(model({in(dll)}).model.records[0].state==State::executed_claim, "clean DLL admitted");
    auto lib=static_fixture(); auto warm_lib=lib;
    warm_lib.result.any_compiled=false; warm_lib.result.archive.archived=false;
    warm_lib.record.archive.completion=ArtifactCompletion::reused; warm_lib.record.archive.cache_state=ArtifactCacheState::reused;
    for(std::size_t i=0;i<2;++i){
        warm_lib.record.sources[i].completion=ArtifactCompletion::reused;
        warm_lib.result.compiles[i].result.compiled=false; warm_lib.cache_evidence.compiles[i].state=CompileCacheEvidenceState::reused;
    }
    auto statics=model({in(lib),in(warm_lib,"reuse")});
    expect(statics.model.records[1].state==State::explicit_reuse &&
           statics.model.references.records[0].stages.back().kind==ArtifactStageKind::archive, "static cold/reuse uses same lineage");
    expect(!statics.model.references.records[0].stages.back().configuration, "LIB stage configuration remains unknown");
    // Strict errors retain both the record slot and the original source slot.
    const auto reject=[&](auto edit, RecordedStorageProjectionIssue code, std::optional<std::size_t> slot) {
        auto damaged=a; edit(damaged);
        auto value=model_recorded_artifact_generations(std::vector{in(a),in(damaged,"bad")},{},root(),lexical_key);
        expect(!value && value.error().record_index==1 && value.error().projection_error &&
               value.error().projection_error->issue==code && value.error().projection_error->source_index==slot,
               "nested projector error identity retained");
    };
    using PI=RecordedStorageProjectionIssue;
    reject([](auto& r){r.cache_evidence.compiles.pop_back();},PI::source_count,{});
    reject([](auto& r){std::swap(r.cache_evidence.compiles[0],r.cache_evidence.compiles[1]);},PI::source_mismatch,0);
    reject([](auto& r){r.cache_evidence.compiles[0].cache_entry.source="wrong";},PI::cache_mismatch,0);
    reject([](auto& r){r.cache_evidence.compiles[0].request.options.defines={"different"};},PI::options_mismatch,0);
    reject([](auto& r){r.result.any_compiled=false;},PI::outcome_mismatch,{});
    reject([](auto& r){std::swap(r.record.link.association.objects[0],r.record.link.association.objects[1]);},PI::terminal_mismatch,{});
    reject([](auto& r){r.record.link.association.side_outputs={r.record.sources[0].source};},PI::path_conflict,{});
    reject([](auto& r){r.record.link.association.objects[0]="other.obj";},PI::terminal_mismatch,{});
    auto both=dll; both.record.link.association.file_inputs.push_back(root()/"plugin.lib");
    auto conflict=model_recorded_artifact_generations(std::vector{in(both)},{},root(),lexical_key);
    expect(!conflict && conflict.error().projection_error->issue==PI::path_conflict, "genuine DLL role conflict not waived");
    // Borrowed inputs produce an owned result; old values cannot be overwritten.
    auto owned=model({in(failed)});
    const auto signature=owned.compiles[0][0].captured_signature;
    failed.cache_evidence.compiles[0].cache_entry.signature=BuildSignature::from_digest({999,999});
    failed.cache_evidence.compiles[0].save_error->message="changed";
    expect(owned.compiles[0][0].captured_signature==signature && owned.compiles[0][0].save_error->message=="locked",
           "owning context survives caller mutation");
    // Optional snapshots corroborate only the terminal link and remain owned.
    auto with_snapshot=in(a);
    LinkFactSnapshot snap;
    snap.completion=ArtifactCompletion::executed; snap.cache_state=ArtifactCacheState::saved; snap.linked=true;
    auto text_path=[](const fs::path& path){const auto u=path.u8string();return std::string{reinterpret_cast<const char*>(u.data()),u.size()};};
    snap.output=text_path(a.record.link.association.output);snap.cache_file=text_path(a.record.link.cache_file);
    snap.working_directory=text_path(*a.record.link.working_directory);snap.signature=a.record.link.association.signature.digest();
    snap.linker_path=text_path(a.record.link.association.linker.linker);snap.linker_version="v1";snap.linker_stamp="stamp";
    with_snapshot.snapshot=snap;
    auto snapshot_model=model({with_snapshot});
    expect(snapshot_model.model.records[0].snapshot==snap && snapshot_model.model.records[0].state==State::executed_claim,
           "matching historical snapshot remains separate corroboration");
    with_snapshot.snapshot->signature.high=88;
    expect(has(model({with_snapshot}).model.records[0],Issue::snapshot_mismatch), "snapshot cannot override terminal identity");
    auto wrong_static=in(lib);wrong_static.snapshot=snap;
    expect(has(model({wrong_static}).model.records[0],Issue::snapshot_mismatch), "link snapshot does not attest a static archive");
    auto relative=a;
    for(std::size_t i=0;i<2;++i){
        auto& s=relative.record.sources[i];auto& v=relative.cache_evidence.compiles[i];
        s.source=s.source.filename();s.object=s.object.filename();s.dependencies=s.dependencies.filename();s.compile_cache=s.compile_cache.filename();
        relative.result.compiles[i].source=s.source;v.request.unit.source=s.source;v.cache_entry.source=s.source;
        v.request.unit.outputs[0].path=s.object;v.cache_entry.outputs[0].path=s.object;
        v.request.cache_file=s.compile_cache;v.request.source_dependencies_file=s.dependencies;v.request.working_directory=root();
        relative.record.link.association.objects[i]=s.object;
    }
    auto resolved=model({in(relative)});
    expect(!has(resolved.model.records[0],Issue::unresolved_path), "own cwd resolves relative source paths");
    relative.cache_evidence.compiles[1].request.working_directory=root()/"conflict";
    auto cwd_conflict=model_recorded_artifact_generations(std::vector{in(relative)},{},root(),lexical_key);
    expect(!cwd_conflict && cwd_conflict.error().projection_error &&
           cwd_conflict.error().projection_error->issue==PI::terminal_mismatch, "different terminal cwd cannot supply a compile object");
    auto generations=model({in(a),in(a,"second","g2")},{retain()});
    expect(std::any_of(generations.model.paths.begin(),generations.model.paths.end(),[](const auto& path){
        return path.multiple_executed_generations && path.retained_records==std::vector<std::size_t>{0};
    }),"shared path across generations remains a potential overwrite, not garbage");
    auto expired=[]{
        auto temporary=fixture();
        return model({in(temporary)});
    }();
    expect(expired.compiles[0][1].cache_toolchain.binary_stamp=="compiler-1", "owned result survives borrowed invocation destruction");
    const auto bounded=[&](auto records, auto retention) {
        unsigned keys=0;
        auto value=model_recorded_artifact_generations(records,retention,root(),[&](const fs::path& path){
            ++keys; return lexical_key(path);
        });
        expect(!value && keys==0,"whole-batch limit failure precedes projection callbacks and result copies");
    };
    auto long_id=in(a,"late"); long_id.source_id.assign(ArtifactGenerationLimits::text_bytes+1,'x');
    bounded(std::vector{in(a),long_id},std::vector<ArtifactGenerationKey>{});
    auto many=a; many.cache_evidence.compiles[0].cache_entry.dependencies.resize(ArtifactGenerationLimits::paths+1,root()/"h.hpp");
    bounded(std::vector{in(a),in(many,"late")},std::vector<ArtifactGenerationKey>{});
    auto too_many=a; too_many.record.sources.resize(ArtifactGenerationLimits::stages,too_many.record.sources[0]);
    bounded(std::vector{in(a),in(too_many,"late")},std::vector<ArtifactGenerationKey>{});
    bounded(std::vector(ArtifactGenerationLimits::records+1,in(a)),std::vector<ArtifactGenerationKey>{});
    auto bad_retention=retain();bad_retention.generation.assign(ArtifactGenerationLimits::text_bytes+1,'x');
    bounded(std::vector{in(a)},std::vector{bad_retention});
    expect(!model_recorded_artifact_generations(std::vector{in(a)},{},root(),{}), "missing key refused");
    bool propagated=false;
    try{(void)model_recorded_artifact_generations(std::vector{in(a)},{},root(),[](const fs::path&)->std::string{
        throw std::runtime_error("rich-key");});}catch(const std::runtime_error& ex){propagated=std::string_view(ex.what())=="rich-key";}
    expect(propagated,"callback exceptions propagate");
    expect(model({}).model.records.empty(), "empty rich input creates nothing");
    std::cout<<count<<" recorded generation checks passed\n";
}
} // namespace recorded_generation_contracts
// END recorded generation model contracts

int main(){try{run();std::cout<<checks<<" generation-model checks passed\n";recorded_generation_contracts::run();return 0;}
catch(const std::exception& e){std::cerr<<"check "<<checks<<": "<<e.what()<<'\n';return 1;}}
