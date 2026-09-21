#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/orchestration/ArtifactStorageProjection.hpp"
#include "mqb/orchestration/MsvcModuleTargetCoordinator.hpp"
#ifdef _WIN32
#include "StorageReport.hpp"
#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/orchestration/MsvcIncrementalTargetCoordinator.hpp"
#include "mqb/platform/windows/PathIdentity.hpp"
#include "mqb/platform/windows/StorageInventory.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"
#endif

namespace {
namespace fs = std::filesystem;
using namespace mqb;
using namespace mqb::orchestration;
using Role = ArtifactPathRole;
using State = ArtifactPathObservation;
void require(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string{message});
}
std::string text(const fs::path& path) {
    auto b=path.generic_u8string(); return {reinterpret_cast<const char*>(b.data()),b.size()};
}
std::string key(const fs::path& path) { return text(path.lexically_normal()); }
LinkArtifactRecord link_record(const fs::path& root) {
    return {.completion=ArtifactCompletion::executed, .cache_state=ArtifactCacheState::saved,
        .association={.signature=BuildSignature::from_digest({1,2}),
            .objects={root/"obj/shared.obj"}, .output=root/"bin/app.exe"},
        .options={}, .cache_file=root/"cache/link/app.linkcache", .working_directory=root.parent_path()};
}
StorageEntry file(const char* path, const char* id, std::uint64_t size=10) {
    return {.relative_path=path, .kind=StorageEntryKind::file, .logical_bytes=size,
        .allocated_bytes=4096, .hard_links=2, .physical_id=id, .protected_path=true};
}
ArtifactStorageReferences refs(const fs::path& root) {
    return {.caller_label=ArtifactGenerationLabel{"same","same"}, .stages={{
        .kind=ArtifactStageKind::compile, .completion=ArtifactCompletion::executed,
        .configuration=BuildConfiguration::debug, .working_directory=root.parent_path(),
        .paths={{root/"obj/shared.obj",Role::declared_output}}}}};
}
void association_contracts() {
    static_assert(!ArtifactStorageAssociation::deletion_authorized);
    static_assert(!ArtifactStorageAssociation::invocation_identity_verified);
    static_assert(!ArtifactStorageAssociation::current_contents_verified);
    static_assert(!ArtifactStorageReferences::complete_reference_inventory);
    const auto root=fs::absolute("association-model/.mqb");
    StorageInventory inventory{.artifact_root=root, .root_exists=true,
        .entries={file("obj/shared.obj","v:1"),file("obj/alias.obj","v:1"),file("logs/note.txt","v:2")}};
    std::array records{refs(root),refs(root)};
    records[1].stages[0].configuration=BuildConfiguration::release;
    records[1].stages[0].completion=ArtifactCompletion::reused;
    records[0].stages[0].paths.push_back({root/"obj/shared.obj",Role::input});
    auto value=associate_artifact_storage(records,inventory,key);
    require(value && value->matches.size()==3 && value->row_matches[0].size()==3,"shared mentions not collapsed by labels");
    require(value->row_matches[1].empty() && value->row_matches[2].empty(),"alias and unmentioned rows not direct matches");
    require(value->matches[0].same_observed_file_rows==std::vector<std::size_t>{1},"observed alias separated from path match");
    require(value->records[1].stages[0].configuration==BuildConfiguration::release &&
        value->records[1].stages[0].completion==ArtifactCompletion::reused,"configuration and reuse retained");
    inventory.entries[0].logical_bytes=99;
    records[0].caller_label->target="changed";
    require(value->observation.entries[0].logical_bytes==10 && value->records[0].caller_label->target=="same",
        "association owns records and observation independently");
    const auto conflict=associate_artifact_storage(records,inventory,key);
    require(conflict && conflict->matches[0].observed_identity_metadata_conflict,"contradictory observed ID metadata flagged");
    inventory.entries[0].logical_bytes=10;inventory.entries[0].hard_links=1;
    require(associate_artifact_storage(records,inventory,key)->matches[0].observed_identity_metadata_conflict,
        "observed name count exceeding link count is not silently coherent");
    inventory.entries[0].hard_links=2;
    inventory.entries.push_back(file("obj/shared.obj","other:3"));
    const auto duplicate=associate_artifact_storage(records,inventory,key);
    require(duplicate && duplicate->matches[0].state==State::ambiguous_path &&
        duplicate->matches[0].observed_rows.size()==2 && duplicate->matches[0].same_observed_file_rows.empty(),
        "duplicate path identity cannot pick a winner");
    inventory.entries.resize(3);
    inventory.entries[0].physical_id.clear();
    require(associate_artifact_storage(records,inventory,key)->matches[0].same_observed_file_rows.empty(),
        "missing ID cannot fabricate observed aliases");
    inventory.entries[0].physical_id="v:1";
    auto case_inventory=inventory;case_inventory.entries.push_back(file("OBJ/SHARED.OBJ","v:other"));
    auto folded=[](const fs::path& p){auto k=key(p);for(auto& c:k)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return k;};
    require(associate_artifact_storage(records,case_inventory,folded)->matches[0].state==State::ambiguous_path,
        "platform key collisions preserve multiple physical observations");
    auto unicode=refs(root);unicode.stages[0].paths={{root/fs::path{u8"\u65e5 space.obj"},Role::declared_output}};
    auto unicode_inventory=inventory;auto unicode_entry=file("placeholder","unicode:id");
    unicode_entry.relative_path=fs::path{u8"\u65e5 space.obj"};unicode_inventory.entries.push_back(unicode_entry);
    require(associate_artifact_storage(std::array{unicode},unicode_inventory,key)->matches[0].state==State::observed_path,
        "Unicode path values survive pure projection and join");
    auto& stage=records[0].stages[0];
    stage.paths={{".mqb/obj/shared.obj",Role::input},{"../shared.obj",Role::input},{"",Role::input},
        {root/"never-seen.obj",Role::declared_output},{fs::path{std::string{"bad\0path",8}},Role::input}};
    inventory.issues.push_back({"locked", "partial original scan", 32});
    const auto paths=associate_artifact_storage(records,inventory,key);
    require(paths && paths->matches[0].state==State::observed_path && paths->matches[1].state==State::unresolved_path &&
        paths->matches[2].state==State::unresolved_path && paths->matches[3].state==State::not_observed &&
        paths->matches[4].state==State::unresolved_path && paths->observation.issues.size()==1,"relative/invalid/partial evidence distinguished");
    stage.working_directory.reset();
    require(associate_artifact_storage(records,inventory,key)->matches[0].state==State::unresolved_path,"no guessed cwd");
    stage.working_directory="relative";
    require(associate_artifact_storage(records,inventory,key)->matches[0].state==State::unresolved_path,"cwd must be recorded absolute");
    inventory.entries[0].kind=StorageEntryKind::reparse_point;
    records[0]=refs(root);
    const auto reparse=associate_artifact_storage(records,inventory,key);
    require(reparse && reparse->matches[0].same_observed_file_rows.empty() &&
        reparse->observation.entries[0].protected_path,"reparse matches stay protected and do not acquire regular-file aliases");
    inventory.entries[0].relative_path="../escape";
    require(!associate_artifact_storage(records,inventory,key),"invalid observation cannot escape root");
    inventory.entries[0].relative_path=root/"obj/shared.obj";
    require(!associate_artifact_storage(records,inventory,key),"rooted inventory row rejected");
    inventory.entries[0].relative_path="obj/shared.obj";
    require(!associate_artifact_storage(records,inventory,{}),"missing path authority rejected");
    require(!associate_artifact_storage(records,inventory,[](const auto&){return std::string{};}),"empty key refused");
    bool threw=false;
    try {(void)associate_artifact_storage(records,inventory,[](const auto&)->std::string {throw std::runtime_error("key failure");});}
    catch (const std::runtime_error&) {threw=true;}
    require(threw,"callback failure cannot manufacture a partial success");
    inventory={.artifact_root=root};
    const auto absent=associate_artifact_storage(records,inventory,key);
    require(absent && !absent->observation.root_exists && absent->matches[0].state==State::not_observed,"unobserved root is not guessed present");
    auto many=refs(root);many.stages[0].paths.assign(1100,{root/"obj/shared.obj",Role::input});
    auto many_rows=StorageInventory{.artifact_root=root,.root_exists=true};
    for(unsigned i=0;i<1100;++i){auto e=file("obj/shared.obj","one:id");
        if(i)e.relative_path="obj/alias-"+std::to_string(i);e.hard_links=1100;many_rows.entries.push_back(e);}
    require(!associate_artifact_storage(std::array{many},many_rows,key),"multiplicative alias expansion is bounded, never truncated");
    require(!fs::exists(root),"portable join/model did not create its synthetic root");
}
void projection_contracts() {
    const auto root=fs::absolute("projection-model/.mqb");
    auto link=link_record(root);
    link.cache_state=ArtifactCacheState::save_failed;
    link.association.side_outputs={root/"bin/app.map"};
    auto terminal=project_storage_references(link);
    require(terminal.stages[0].paths.size()==4 && terminal.stages[0].cache_state==ArtifactCacheState::save_failed,
        "actual link side output and save state projected");
    TargetArtifactRecord ordinary{.caller_label=ArtifactGenerationLabel{"target","old"}, .compiler_options={},
        .sources={{.source=root.parent_path()/"main.cpp",.object=root/"obj/shared.obj",
            .dependencies=root/"deps/main.json",.compile_cache=root/"cache/main",.completion=ArtifactCompletion::reused,
            .has_warnings=true}}, .additional_object_inputs={root/"obj/upstream.obj"},.link=link};
    auto ordinary_paths=project_storage_references(ordinary);
    require(ordinary_paths.stages.size()==2 && !ordinary_paths.stages[0].cache_state &&
        !ordinary_paths.stages[0].working_directory && ordinary_paths.stages[0].source_has_warnings==true,"old source retains warnings but lacks exact save/cwd evidence");
    ArchiveArtifactRecord archive{.completion=ArtifactCompletion::reused,.cache_state=ArtifactCacheState::reused,
        .association={.signature=BuildSignature::from_digest({3,4}),.objects={root/"obj/shared.obj"},.output=root/"bin/app.lib"},
        .architecture=Architecture::x64,.link_time_code_generation=false,.cache_file=root/"cache/lib",.working_directory=root};
    StaticTargetArtifactRecord lib{.compiler_options={},.sources=ordinary.sources,.archive=archive};
    require(project_storage_references(lib).stages.size()==2 &&
        !project_storage_references(archive).stages[0].configuration,"archive signature does not invent compile configuration");
    PchArtifactRecord pch{.completion=ArtifactCompletion::reused,.cache_state=ArtifactCacheState::reused,
        .input_header=root.parent_path()/"pch.hpp",.creator={.source=root/"pch/creator.cpp",
            .outputs={{root/"pch/creator.obj",ArtifactKind::object},{root/"pch/creator.pch",ArtifactKind::precompiled_header}}},
        .compiler_options={},.dependencies=root/"pch/deps",.compile_cache=root/"pch/cache",.working_directory=root};
    auto p=project_storage_references(pch);
    require(p.stages[0].paths.size()==7 && p.stages[0].completion==ArtifactCompletion::reused,"PCH input/creator/pair/metadata retained without new-generation claim");
    ModuleCompileArtifactRecord header{.completion=ArtifactCompletion::reused,.cache_state=ArtifactCacheState::reused,
        .unit={.source=root.parent_path()/"extra.hpp",.header_unit=HeaderUnitIdentity{"extra.hpp"},
            .module_references={{"External",root.parent_path()/"external.ifc"}},
            .outputs={{root/"ifc/extra.ifc",ArtifactKind::module_interface}}},
        .compiler_options={},.dependencies=root/"deps/extra",.compile_cache=root/"cache/extra",.working_directory=root};
    const auto h=project_storage_references(header);
    require(std::count_if(h.stages[0].paths.begin(),h.stages[0].paths.end(),[](const auto& v){return v.role==Role::declared_output;})==1,
        "IFC-only header unit never fabricates object");
    ModuleScanArtifactRecord scan{.completion=ArtifactCompletion::executed,
        .recipe={.invocation={.source=root.parent_path()/"module.ixx",.output_file=root/"scan/module.json"}},
        .compile_cache_reference=root/"cache/module"};
    scan.dependencies.rules.push_back({.primary_output=root/"invented.obj"});
    ModuleTargetArtifactRecord module{.caller_label=ArtifactGenerationLabel{"module","one"},
        .scans={{scan,false},{scan,true}},.compiles={.header_unit_compiles={header}},.link=link};
    auto m=project_storage_references(module);
    require(m.stages.size()==4 && !m.stages[0].toolchain_source && m.stages[1].toolchain_source &&
        !m.stages[0].cache_state && m.stages[0].paths.size()==3,"scan provenance preserved without fake cache save or P1689 output");
    module.scans[0].scan.recipe.invocation.source="mutated";
    require(m.stages[0].paths[0].path!=fs::path{"mutated"},"projection owns its values");
    std::cout<<"portable projection and association contracts passed\n";
}
#ifdef _WIN32
void write(const fs::path& path,std::string_view value) {
    fs::create_directories(path.parent_path()); std::ofstream out{path,std::ios::binary};
    out<<value;out.close(); require(static_cast<bool>(out),"write fixture/evidence");
}
std::string bytes(const fs::path& path) {
    std::ifstream in{path,std::ios::binary};require(in.is_open(),"open file snapshot");
    std::string out{std::istreambuf_iterator<char>{in},{}};require(!in.bad(),"read file snapshot");return out;
}
std::map<std::string,std::pair<fs::file_time_type,std::string>> snapshot(const fs::path& root) {
    std::map<std::string,std::pair<fs::file_time_type,std::string>> out;
    for (const auto& e:fs::recursive_directory_iterator(root)) if(e.is_regular_file())
        out.emplace(text(e.path().lexically_relative(root)),std::make_pair(e.last_write_time(),bytes(e.path())));
    return out;
}
void native_contracts(const fs::path& root,const fs::path& evidence) {
    struct Runner final:process::ProcessRunner {
        platform::windows::WindowsProcessRunner native;
        fs::path evidence;std::string phase{"discovery"};unsigned calls{},tools{};
        explicit Runner(fs::path p):evidence(std::move(p)){}
        std::expected<process::ProcessResult,process::ProcessError> run(const process::ProcessSpec& spec) override {
            if(phase!="discovery" && phase!="program" && phase!="fixture-junction") require(++tools<=8,"fixed compile/link budget");
            const auto p=evidence/(std::to_string(++calls)+"-"+phase);
            // Do not publish inherited environment values or discovery dumps.
            if(phase!="discovery") {
                std::ostringstream cmd;cmd<<"executable="<<text(spec.executable)<<'\n';
                for(const auto& a:spec.arguments)cmd<<"arg="<<a<<'\n';
                write(p.string()+".argv.txt",cmd.str());
            }
            const auto r=native.run(spec);
            if(phase!="discovery") {
                if(!r)write(p.string()+".error.txt",r.error().message);
                else{write(p.string()+".stdout.txt",r->stdout_text);write(p.string()+".stderr.txt",r->stderr_text);
                    write(p.string()+".exit.txt",std::to_string(r->exit_code));}
            }
            return r;
        }
    } runner{evidence/"processes"};
    fs::create_directories(root);
    msvc::MsvcToolchainLocator locator{runner};msvc::DiscoveryOptions discovery;
    discovery.preference=msvc::ToolchainPreference::visual_studio;
    discovery.cache_file=root/".mqb/cache/toolchain/vs-x64.cache";
    const auto toolchain=locator.discover(discovery);require(toolchain.has_value(),"native toolchain discovery");
    msvc::MsvcCompileExecutor executor{*toolchain,runner};MsvcIncrementalCompileCoordinator compiling{*toolchain,executor};
    msvc::MsvcLinker linker{*toolchain,runner};MsvcIncrementalLinkCoordinator linking{*toolchain,linker};
    MsvcIncrementalTargetCoordinator target{compiling,linking};
    const auto layout=ProjectArtifactLayout::create(root);require(layout.has_value(),"native layout");
    write(root/"main.cpp","int main(){return 0;}\n");
    const auto source=layout->for_source(root/"main.cpp");require(source.has_value(),"source layout");
    IncrementalTargetRequest request;request.working_directory=root;request.max_parallel_compiles=1;
    request.sources={{root/"main.cpp",*source}};request.link_options.additional_arguments={"/INCREMENTAL:NO"};
    auto set_target=[&](const char* name){const auto a=layout->for_target(name);require(a.has_value(),"target layout");request.target=*a;};
    set_target("app");std::vector<ArtifactStorageReferences> records;unsigned builds{},scans{};
    auto run=[&](const char* phase){
        require(++builds<=4,"four target calls only");runner.phase=phase;
        write(evidence/(std::string{phase}+".attempt.txt"),bytes(root/"main.cpp"));
        const auto r=target.run_recorded(request,ArtifactGenerationLabel{"same-label",phase});
        if(!r)write(evidence/(std::string{phase}+".error.txt"),r.error().message);
        require(r.has_value(),"native recorded target success");records.push_back(project_storage_references(r->record));return r;
    };
    auto observe=[&](const char* phase){
        require(++scans<=8,"eight inventory observations maximum");
        auto inv=platform::windows::scan_storage(root/".mqb");
        std::ostringstream raw;require(mqb::app::diagnostics::write_storage_report(raw,inv,true),"raw inventory report");
        write(evidence/(std::string{phase}+".inventory.json"),raw.str());
        auto a=associate_artifact_storage(records,inv,platform::windows::path_identity_key);
        if(!a)write(evidence/(std::string{phase}+".error.txt"),a.error().message);
        require(a.has_value(),"join native observation");
        std::ostringstream rows;rows<<"identity_verified=false\ncontents_verified=false\ndeletion_authorized=false\n";
        for(std::size_t r=0;r<a->records.size();++r)for(std::size_t s=0;s<a->records[r].stages.size();++s){
            const auto& stage=a->records[r].stages[s];
            rows<<"stage="<<r<<':'<<s<<'|'<<static_cast<int>(stage.kind)<<'|'<<static_cast<int>(stage.completion)<<'\n';
            if(stage.configuration)rows<<"configuration="<<to_string(*stage.configuration)<<'\n';
            if(stage.cache_state)rows<<"cache_state="<<static_cast<int>(*stage.cache_state)<<'\n';
            if(stage.source_has_warnings)rows<<"source_has_warnings="<<*stage.source_has_warnings<<'\n';
            for(std::size_t p=0;p<stage.paths.size();++p)rows<<"reference="<<r<<':'<<s<<':'<<p<<'|'
                <<static_cast<int>(stage.paths[p].role)<<'|'<<text(stage.paths[p].path)<<'\n';
        }
        for(const auto& m:a->matches){
            rows<<"match="<<m.record_index<<':'<<m.stage_index<<':'<<m.path_index<<'|'<<static_cast<int>(m.state)<<'\n';
            for(auto row:m.observed_rows)rows<<"row="<<row<<'\n';
            for(auto row:m.same_observed_file_rows)rows<<"observed_alias="<<row<<'\n';
            rows<<"observed_metadata_conflict="<<m.observed_identity_metadata_conflict<<'\n';
        }
        write(evidence/(std::string{phase}+".association.txt"),rows.str());return *a;
    };
    const auto cold=run("01-cold");const auto before=snapshot(root/".mqb");const auto calls=runner.calls;
    const auto warm=run("02-reuse");require(warm->record.link.completion==ArtifactCompletion::reused && calls==runner.calls,"warm no tool calls");
    auto first=observe("01-cold-warm");require(first.observation.issues.empty() && before==snapshot(root/".mqb"),"reuse and join/scan preserve fixture bytes and mtime");
    set_target("shared");const auto shared=run("03-shared");require(!shared->result.any_compiled,"shared target reuses object");
    set_target("app");request.compiler_options.configuration=BuildConfiguration::release;
    request.link_options.configuration=BuildConfiguration::release;run("04-release");
    auto overwritten=observe("02-configuration");require(overwritten.records[0].stages[0].configuration==BuildConfiguration::debug &&
        overwritten.records[3].stages[0].configuration==BuildConfiguration::release,"same current path does not erase historical config");
    runner.phase="program";process::ProcessSpec program;program.executable=request.target.executable;program.working_directory=root;
    program.capture_stdout=program.capture_stderr=true;const auto executed=runner.run(program);
    require(executed && executed->exit_code==0,"native consumer before fixture mutations");
    const auto object=cold->record.sources[0].object;const auto alias=object.parent_path()/"alias.obj";
    const bool made=::CreateHardLinkW(alias.c_str(),object.c_str(),nullptr)!=FALSE;
    write(evidence/"hardlink.txt",std::to_string(made ? 0 : ::GetLastError()));require(made,"real hardlink fixture");
    auto linked=observe("03-hardlink");bool saw_alias=false;
    for(const auto& m:linked.matches)if(m.resolved_path==object && !m.same_observed_file_rows.empty())saw_alias=true;
    require(saw_alias,"historical object path exposes observed hardlink aliases");
    require(fs::remove(object),"remove only fixture path, keep hardlink");
    auto missing=observe("04-path-removed");bool missing_reference=false;
    for(const auto& m:missing.matches)if(m.resolved_path==object)missing_reference|=m.state==State::not_observed;
    require(missing_reference && fs::exists(alias),"missing path cannot be rebound to alias from historical record");
    write(object,"replacement bytes, not the successful build's object");
    auto replaced=observe("05-path-replaced");bool new_file=false;
    for(const auto& m:replaced.matches)if(m.resolved_path==object)new_file|=m.state==State::observed_path && m.same_observed_file_rows.empty();
    require(new_file,"same path replacement is only an observation, never content identity");
    auto blocked=[&]{
        struct Handle { HANDLE value; ~Handle(){if(value!=INVALID_HANDLE_VALUE)::CloseHandle(value);} };
        const Handle busy{::CreateFileW(object.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr)};
        require(busy.value!=INVALID_HANDLE_VALUE,"open writer fixture");
        return observe("06-busy");
    }();
    bool unavailable=false;
    for(const auto& m:blocked.matches)if(m.resolved_path==object)for(const auto row:m.observed_rows)
        unavailable|=blocked.observation.entries[row].kind==StorageEntryKind::unavailable;
    require(!blocked.observation.issues.empty() && unavailable,"writer refusal retains unavailable row, not healthy matched file");
    const auto external=evidence/"outside";write(external/"sentinel.txt","outside fixture must not be traversed");
    auto native_text=[](const fs::path& p){const auto b=p.u8string();return std::string{reinterpret_cast<const char*>(b.data()),b.size()};};
    process::ProcessSpec junction;junction.executable=L"C:/Windows/System32/cmd.exe";
    junction.arguments={"/d","/c","mklink","/J",native_text(root/".mqb/junction"),native_text(external)};
    junction.working_directory=root;junction.capture_stdout=junction.capture_stderr=true;
    runner.phase="fixture-junction";const auto made_junction=runner.run(junction);
    require(made_junction && made_junction->exit_code==0,"create native junction fixture");
    require(fs::equivalent(root/".mqb/junction",external),"fixture junction points to expected external directory");
    records[0].stages[0].paths.push_back({root/".mqb/junction",Role::metadata_reference});
    auto skipped=observe("07-junction");bool protected_junction=false;
    for(const auto& row:skipped.observation.entries){
        require(text(row.relative_path).find("sentinel.txt")==std::string::npos,"join did not traverse junction");
        if(row.relative_path==fs::path{"junction"})protected_junction=row.kind==StorageEntryKind::reparse_point && row.protected_path;
    }
    require(protected_junction && !skipped.observation.issues.empty() && fs::exists(external/"sentinel.txt"),
        "matched reparse row retains refused observation and protection");
    require(first.observation.issues.empty() && first.records.size()==2,"earlier returned snapshot unaffected");
    require(builds==4 && scans==7,"fixed target/observation count");
    write(evidence/"completed.txt","4 real target calls; 7 observations; 1 program; 1 junction command; hardlink/missing/replacement/busy/reparse; no cleanup authority\n");
}
#endif
} // namespace
int main() {
    try {
        association_contracts();projection_contracts();
#ifdef _WIN32
        const auto work=fs::current_path();require(!fs::exists(work/"storage-fixtures") && !fs::exists(work/"storage-evidence"),"fresh evidence required");
        native_contracts(work/"storage-fixtures"/fs::path{L"association space \u65e5"},work/"storage-evidence/association");
#else
        std::cout<<"Windows scanner, compiler and file mutation fixtures NOT executed\n";
#endif
        return 0;
    } catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
