#include "mqb/orchestration/ArtifactGenerationModel.hpp"

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
int main(){try{run();std::cout<<checks<<" generation-model checks passed\n";return 0;}
catch(const std::exception& e){std::cerr<<"check "<<checks<<": "<<e.what()<<'\n';return 1;}}
