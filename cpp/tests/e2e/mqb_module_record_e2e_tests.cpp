#include <algorithm>
#include <atomic>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "mqb/orchestration/MsvcModuleCompileCoordinator.hpp"
#ifdef _WIN32
#include "mqb/msvc/MsvcModuleDependencyScanner.hpp"
#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"
#endif

namespace {
namespace fs = std::filesystem;
using namespace mqb;
using namespace mqb::orchestration;
void require(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string{message});
}
void model_contracts() {
    static_assert(!ModuleCompileArtifactRecord::exact_cache_entry_captured);
    static_assert(!ModuleCompileArtifactRecord::physical_identity_verified);
    static_assert(!ModuleCompileArtifactRecord::complete_producer_inventory);
    static_assert(!ModuleCompileArtifactRecord::deletion_authorized);
    static_assert(!ModuleCompileWaveArtifactRecord::deletion_authorized);
    ModuleCompileArtifactRecord provider{
        .completion=ArtifactCompletion::executed, .cache_state=ArtifactCacheState::saved,
        .unit={.source="A.ixx", .kind=TranslationUnitKind::module_interface,
            .outputs={{"A.obj",ArtifactKind::object},{"A.ifc",ArtifactKind::module_interface}}},
        .compiler_options={}, .dependencies="A.deps", .compile_cache="A.cache",
        .module_scan_output="A.p1689", .working_directory="project"};
    ModuleCompileWaveArtifactRecord first;
    first.caller_label=ArtifactGenerationLabel{"target","old"};
    first.compiles.push_back(provider);
    first.dependencies.resolved_dependencies.push_back({"consumer.cpp","A.ixx","A"});
    auto second=first;
    second.compiles[0].unit.outputs[0].path="different.obj";
    second.compiles[0].compiler_options.configuration=BuildConfiguration::release;
    second.dependencies.resolved_dependencies[0].logical_name="different";
    second.caller_label->generation="new";
    require(first.compiles[0].unit.outputs[0].path=="A.obj" &&
        first.compiles[0].compiler_options.configuration==BuildConfiguration::debug &&
        first.dependencies.resolved_dependencies[0].logical_name=="A" &&
        first.caller_label->generation=="old", "module wave record owns its values");
}
#ifdef _WIN32
std::string text(const fs::path& path) {
    const auto b=path.generic_u8string();
    return {reinterpret_cast<const char*>(b.data()),b.size()};
}
fs::path path_from_utf8(std::string_view value) {
    return fs::path{std::u8string{reinterpret_cast<const char8_t*>(value.data()),value.size()}};
}
void write(const fs::path& path, std::string_view value) {
    fs::create_directories(path.parent_path());
    std::ofstream out{path,std::ios::binary};
    out.write(value.data(),static_cast<std::streamsize>(value.size())); out.close();
    require(static_cast<bool>(out),"write module fixture/evidence");
}
std::string bytes(const fs::path& path) {
    std::ifstream in{path,std::ios::binary}; require(in.is_open(),"open module evidence source");
    std::string result{std::istreambuf_iterator<char>{in},std::istreambuf_iterator<char>{}};
    require(!in.bad(),"read module evidence source"); return result;
}
struct FileState {
    fs::file_time_type modified;
    std::string contents;
    bool operator==(const FileState&) const = default;
};
std::map<std::string,FileState> snapshot(const fs::path& root) {
    std::map<std::string,FileState> result;
    if (fs::exists(root)) for (const auto& e:fs::recursive_directory_iterator(root))
        if (e.is_regular_file()) result.emplace(text(e.path().lexically_relative(root)),
            FileState{e.last_write_time(),bytes(e.path())});
    return result;
}
void log_process(const fs::path& prefix,const process::ProcessSpec& spec) {
    std::ostringstream out; out<<"executable="<<text(spec.executable)<<'\n';
    if (spec.working_directory) out<<"cwd="<<text(*spec.working_directory)<<'\n';
    for (const auto& a:spec.arguments) out<<"arg="<<a<<'\n';
    write(prefix.string()+".argv.txt",out.str());
}
void log_result(const fs::path& prefix,const std::expected<process::ProcessResult,process::ProcessError>& r) {
    if (!r) write(prefix.string()+".launch-error.txt",r.error().message);
    else {
        write(prefix.string()+".exit.txt",std::to_string(r->exit_code));
        write(prefix.string()+".stdout.txt",r->stdout_text);
        write(prefix.string()+".stderr.txt",r->stderr_text);
    }
}
void describe_node(std::ostream& out,const ModuleCompileArtifactRecord& r) {
    out<<"source="<<text(r.unit.source)<<"\ncompletion="
        <<(r.completion==ArtifactCompletion::executed?"executed":"reused")
        <<"\ncache_state="<<(r.cache_state==ArtifactCacheState::saved?"saved":
            r.cache_state==ArtifactCacheState::reused?"reused":"save_failed")
        <<"\nconfiguration="<<to_string(r.compiler_options.configuration)
        <<"\nforce_rebuild="<<r.force_rebuild<<"\ncache="<<text(r.compile_cache)
        <<"\ndependencies="<<text(r.dependencies)<<'\n';
    if (r.working_directory) out<<"cwd="<<text(*r.working_directory)<<'\n';
    if (r.module_scan_output) out<<"prior_scan="<<text(*r.module_scan_output)<<'\n';
    if (r.unit.header_unit) out<<"header="<<r.unit.header_unit->header_name<<"\nlookup="
        <<(r.unit.header_unit->lookup_method==HeaderUnitLookupMethod::quote?"quote":"angle")<<'\n';
    for (const auto& a:r.unit.outputs) out<<"output="<<static_cast<int>(a.kind)<<'|'<<text(a.path)<<'\n';
    for (const auto& ref:r.unit.module_references) out<<"module_input="<<ref.logical_name<<'|'<<text(ref.interface_file)<<'\n';
    for (const auto& ref:r.unit.header_unit_references) out<<"header_input="<<ref.header_name<<'|'<<text(ref.interface_file)<<'\n';
}
void save_call(const fs::path& evidence,const std::string& phase,
    const std::expected<RecordedModuleCompileWaveResult,ModuleCompileError>& r,bool copies) {
    const auto prefix=evidence/phase;
    if (!r) {
        std::ostringstream out;
        out<<"code="<<static_cast<int>(r.error().code)<<"\nsource="<<text(r.error().source)
            <<"\nmessage="<<r.error().message<<'\n';
        if (r.error().compile_error) {
            out<<"compile_code="<<static_cast<int>(r.error().compile_error->code)
                <<"\ncompile_error="<<r.error().compile_error->message<<'\n';
            if (r.error().compile_error->compile_error) out<<"executor_code="
                <<static_cast<int>(r.error().compile_error->compile_error->code)
                <<"\nexecutor_error="<<r.error().compile_error->compile_error->message<<'\n';
        }
        write(prefix.string()+".error.txt",out.str()); return;
    }
    std::ostringstream out;
    out<<"whole_wave_success=true\nexact_cache_entry_captured=false\nphysical_identity_verified=false\n"
        <<"complete_producer_inventory=false\ndeletion_authorized=false\n";
    if (r->record.caller_label) out<<"generation="<<r->record.caller_label->generation<<'\n';
    for (const auto& e:r->record.dependencies.resolved_dependencies)
        out<<"named_edge="<<text(e.consumer_source)<<'|'<<text(e.provider_source)<<'|'<<e.logical_name<<'\n';
    for (const auto& e:r->record.dependencies.resolved_external_dependencies)
        out<<"external_edge="<<text(e.consumer_source)<<'|'<<e.logical_name<<'|'<<text(e.interface_file)<<'\n';
    for (const auto& e:r->record.dependencies.resolved_header_unit_dependencies)
        out<<"header_edge="<<text(e.consumer_source)<<'|'<<text(e.provider_source)<<'|'<<e.header_name<<'\n';
    auto nodes=[&](const auto& records,const auto& results,const char* category) {
        require(records.size()==results.size(),"record/result node cardinality");
        for (std::size_t i=0;i<records.size();++i) {
            const auto& record=records[i]; const auto& result=results[i];
            out<<"node="<<category<<':'<<i<<'\n'; describe_node(out,record);
            out<<"compiled="<<result.result.compiled<<'\n';
            for (const auto& w:result.result.warnings) out<<"warning="<<static_cast<int>(w.code)<<'|'<<w.message<<'\n';
            if (!copies) continue;
            const auto copy=prefix.string()+"."+category+std::to_string(i);
            write(copy+".input",bytes(record.unit.source));
            for (std::size_t j=0;j<record.unit.outputs.size();++j)
                write(copy+".output"+std::to_string(j),bytes(record.unit.outputs[j].path));
            write(copy+".deps.json",bytes(record.dependencies));
            if (record.cache_state!=ArtifactCacheState::save_failed)
                write(copy+".compilecache",bytes(record.compile_cache));
        }
    };
    nodes(r->record.compiles,r->result.compiles,"source");
    nodes(r->record.header_unit_compiles,r->result.header_unit_compiles,"header");
    write(prefix.string()+".record.txt",out.str());
}
ModuleCompileWaveRequest request_for(const fs::path& root,modules::ModuleDependencyPlan plan) {
    const auto layout=ProjectArtifactLayout::create(root); require(layout.has_value(),"module layout");
    ModuleCompileWaveRequest r;
    // Deliberately opposite dependency order; header units have their own list.
    for (const auto& filename:{"main.cpp","B.ixx","A.ixx"}) {
        const auto source=root/filename; const auto a=layout->for_source(source);
        require(a.has_value(),"module artifacts");
        r.sources.push_back({source,*a,std::string_view{filename}=="main.cpp"?
            TranslationUnitKind::source:TranslationUnitKind::module_interface});
    }
    for (const auto& h:plan.header_units) {
        const auto a=layout->for_source(h.source); require(a.has_value(),"header unit artifacts");
        r.header_units.push_back({h.source,h.header_name,h.lookup_method==modules::LookupMethod::include_quote?
            HeaderUnitLookupMethod::quote:HeaderUnitLookupMethod::angle,*a});
    }
    r.plan=std::move(plan); r.compiler_options.standard=CppStandard::latest;
    r.compiler_options.include_directories={root}; r.working_directory=root; r.max_parallel_compiles=2;
    return r;
}
void save_attempt(const fs::path& evidence, const std::string& phase,
                  const ModuleCompileWaveRequest& request, const char* mode) {
    const auto prefix=evidence/phase;
    std::ostringstream out;
    out<<"mode="<<mode<<"\nconfiguration="<<to_string(request.compiler_options.configuration)
        <<"\ncwd="<<text(request.working_directory)<<'\n';
    for (std::size_t i=0;i<request.sources.size();++i) {
        const auto& s=request.sources[i];
        out<<"source="<<text(s.source)<<"\nobject="<<text(s.artifacts.object)
            <<"\nifc_layout="<<text(s.artifacts.module_interface)
            <<"\ncache="<<text(s.artifacts.compile_cache)<<'\n';
        write(prefix.string()+".attempt.source"+std::to_string(i),bytes(s.source));
    }
    for (std::size_t i=0;i<request.header_units.size();++i) {
        const auto& h=request.header_units[i];
        out<<"header_source="<<text(h.source)<<"\nheader_name="<<h.header_name
            <<"\nheader_ifc="<<text(h.artifacts.module_interface)<<'\n';
        write(prefix.string()+".attempt.header"+std::to_string(i),bytes(h.source));
    }
    for (const auto& level:request.plan.compile_levels) {
        out<<"level_begin\n";
        for (const auto& source:level) out<<"node="<<text(source)<<'\n';
    }
    write(prefix.string()+".attempt.txt",out.str());
}
void fixture(const fs::path& root) {
    write(root/"A.ixx","export module Base;\nexport int module_value(){return 7;}\n");
    write(root/"B.ixx","export module Math;\nimport Base;\nexport int combined(){return module_value();}\n");
    write(root/"extra.hpp","#pragma once\ninline int header_value(){return 4;}\n");
    write(root/"main.cpp","import Math;\nimport \"extra.hpp\";\nint main(){return combined()+header_value();}\n");
}
void check_projection(const RecordedModuleCompileWaveResult& r,const ModuleCompileWaveRequest& request) {
    require(r.record.compiles.size()==3 && r.record.header_unit_compiles.size()==1,"all module nodes recorded");
    for (std::size_t i=0;i<3;++i) require(r.record.compiles[i].unit.source==request.sources[i].source,
        "source order independent of worker completion");
    const auto& c=r.record.compiles[0].unit; const auto& h=r.record.header_unit_compiles[0];
    require(c.module_references.size()>=2 && c.header_unit_references.size()==1,"transitive named references and direct header reference");
    require(h.unit.header_unit && h.unit.outputs.size()==1 && h.unit.outputs[0].kind==ArtifactKind::module_interface &&
        !h.module_scan_output,"header producer is IFC-only, with no fabricated scan");
    require(r.record.compiles[2].unit.outputs.size()==2 && c.outputs.size()==1,"named provider paired outputs versus consumer object");
    for (const auto& ref:c.module_references) require(ref.interface_file!=c.outputs[0].path,"imports are not owned outputs");
}
// Minimal sourceDependencies fixture serialization, not a product parser.
std::string fixture_json_string(std::string_view value) {
    std::string out{"\""};
    for (char c:value) { if (c=='\\'||c=='"') out+='\\'; out+=c; }
    return out+'"';
}
void deterministic_cases(const fs::path& root,const fs::path& evidence) {
    struct Mock final:process::ProcessRunner {
        fs::path evidence,fail_source,omit_ifc_source;
        std::string phase;
        std::atomic<unsigned> calls{0};
        explicit Mock(fs::path p):evidence(std::move(p)) {}
        std::expected<process::ProcessResult,process::ProcessError> run(const process::ProcessSpec& spec) override {
            const auto n=++calls; require(n<=16,"mock process budget");
            const auto prefix=evidence/(std::to_string(n)+"-"+phase); log_process(prefix,spec);
            fs::path object,ifc,deps;
            const auto source=path_from_utf8(spec.arguments.back());
            for (std::size_t i=0;i<spec.arguments.size();++i) {
                const auto& a=spec.arguments[i];
                if (a.starts_with("/Fo")) object=path_from_utf8(a.substr(3));
                else if (a=="/ifcOutput" && i+1<spec.arguments.size()) ifc=path_from_utf8(spec.arguments[++i]);
                else if (a=="/sourceDependencies" && i+1<spec.arguments.size()) deps=path_from_utf8(spec.arguments[++i]);
            }
            require(!deps.empty() && (!object.empty()||!ifc.empty()),"actual mock compile recipe outputs");
            const bool failed=source==fail_source;
            if (!failed) {
                if (!object.empty()) write(object,"mock object "+text(source));
                if (!ifc.empty() && source!=omit_ifc_source) write(ifc,"mock IFC "+text(source));
                write(deps,"{\"Version\":\"1.2\",\"Data\":{\"Source\":"+fixture_json_string(text(source))+",\"Includes\":[]}}");
            }
            std::expected<process::ProcessResult,process::ProcessError> r=process::ProcessResult{
                .exit_code=failed?2:0,.stdout_text="mock compiler only",.stderr_text=failed?"MODULE_MOCK_FAILURE":""};
            log_result(prefix,r); return r;
        }
    } runner{evidence/"processes"};
    fixture(root); write(root/"external/prebuilt.ifc","read-only external fixture");
    write(root/"tools/cl.exe","mock compiler identity");
    const auto external_before=snapshot(root/"external");
    // Use the existing typed graph builder, with explicitly synthetic scan data.
    modules::ScannedModuleUnit a{.source=root/"A.ixx",.rule={.provided_modules={{.logical_name="Base"}}}};
    modules::ScannedModuleUnit b{.source=root/"B.ixx",.rule={.provided_modules={{.logical_name="Math"}},
        .required_modules={{.logical_name="Base"}}}};
    modules::ScannedModuleUnit c{.source=root/"main.cpp",.rule={.required_modules={
        {.logical_name="Math"},{.logical_name="External"},
        {.logical_name="extra.hpp",.source_path=root/"extra.hpp",.unique_on_source_path=true,.lookup_method=modules::LookupMethod::include_quote}}}};
    const std::vector<ExternalModuleProvider> external={{"External",root/"external/prebuilt.ifc"}};
    auto graph=modules::ModuleDependencyGraphBuilder::build({c,b,a},external);
    require(graph.has_value(),"synthetic P1689 graph"); auto request=request_for(root,*graph);
    msvc::MsvcToolchain toolchain{.identity={.compiler=root/"tools/cl.exe",.version="module-record-mock",.binary_stamp="fixture"}};
    msvc::MsvcCompileExecutor executor{toolchain,runner}; MsvcIncrementalCompileCoordinator compiling{toolchain,executor};
    MsvcModuleCompileCoordinator wave{compiling}; unsigned calls{};
    auto invoke=[&](const char* phase,const ModuleCompileWaveRequest& input) {
        require(++calls<=10,"mock wave budget"); runner.phase=phase;
        save_attempt(evidence,phase,input,"mock processes; actual graph/wave/cache code");
        auto r=wave.run_recorded(input,ArtifactGenerationLabel{"mock-module",phase}); save_call(evidence,phase,r,false); return r;
    };
    const auto cold=invoke("01-cold",request); require(cold.has_value(),"cold mock wave"); check_projection(*cold,request);
    require(cold->record.dependencies.resolved_external_dependencies.size()==1 && runner.calls==4,"external provider is not compiled");
    const auto before=snapshot(root/".mqb"); const auto count=runner.calls.load();
    const auto reuse=invoke("02-reuse",request);
    require(reuse && !reuse->result.any_compiled && runner.calls==count && before==snapshot(root/".mqb"),"mock reuse has no process or writes");
    require(fs::remove(request.sources[2].artifacts.module_interface),"remove only mock A IFC");
    const auto repair=invoke("03-ifc-repair",request);
    require(repair && repair->record.compiles[2].completion==ArtifactCompletion::executed &&
        repair->record.compiles[1].force_rebuild && repair->record.compiles[0].force_rebuild,"provider repair propagates actual force requests");
    auto warning=request; warning.sources[0].artifacts.compile_cache=root/".mqb/blocked-cache";
    write(warning.sources[0].artifacts.compile_cache/"sentinel","retain obstruction");
    const auto warned=invoke("04-save-failed",warning);
    require(warned && warned->record.compiles[0].cache_state==ArtifactCacheState::save_failed &&
        !warned->result.compiles[0].result.warnings.empty(),"typed cache-save warning not durable success");
    require(fs::remove(request.sources[1].artifacts.module_interface),"remove only mock B IFC before failure");
    runner.fail_source=root/"B.ixx"; const auto before_failure=runner.calls.load();
    const auto failed=invoke("05-provider-failed",request);
    require(!failed && failed.error().code==ModuleCompileErrorCode::compile_failed &&
        failed.error().source==root/"B.ixx" && runner.calls==before_failure+1,
        "failed provider produces no whole-wave success or downstream process");
    runner.fail_source.clear(); runner.omit_ifc_source=root/"B.ixx";
    const auto omitted=invoke("06-missing-ifc",request);
    require(!omitted && omitted.error().compile_error && omitted.error().compile_error->compile_error &&
        omitted.error().compile_error->compile_error->code==msvc::CompileExecutorErrorCode::output_missing &&
        runner.calls==before_failure+2,"zero exit without required IFC rejected before downstream work");
    runner.omit_ifc_source.clear(); const auto validation_count=runner.calls.load();
    auto collision=request; collision.sources[0].artifacts.object=request.sources[1].artifacts.object;
    const auto bad=invoke("07-collision",collision);
    require(!bad && bad.error().code==ModuleCompileErrorCode::artifact_collision,"ambiguous output rejected before work");
    auto unresolved=request; unresolved.plan.unresolved_requirements.push_back({root/"main.cpp",{.logical_name="Missing"}});
    const auto missing=invoke("08-unresolved",unresolved);
    require(!missing && missing.error().code==ModuleCompileErrorCode::unresolved_requirement,"unresolved dependency not guessed");
    auto invalid=request; invalid.sources[2].kind=TranslationUnitKind::source;
    const auto wrong=invoke("09-invalid-provider",invalid);
    require(!wrong && wrong.error().code==ModuleCompileErrorCode::invalid_provider && runner.calls==validation_count,"invalid provider not converted to success");
    auto header=request; header.sources.clear(); header.plan={}; header.plan.header_units=request.plan.header_units;
    header.plan.compile_levels={{request.header_units[0].source}};
    const auto only=invoke("10-header-only",header);
    require(only && only->record.compiles.empty() && only->record.header_unit_compiles.size()==1 &&
        only->record.header_unit_compiles[0].unit.outputs.size()==1,"header-only wave has no fabricated object");
    require(calls==10 && external_before==snapshot(root/"external"),"fixed mock calls; external file untouched");
    write(evidence/"completed.txt","10 waves; 5 successes, 5 expected errors; mock compiler processes="+std::to_string(runner.calls.load())+"\n");
}
#endif
#ifdef _WIN32
void native_cases(const fs::path& root,const fs::path& evidence) {
    struct Runner final:process::ProcessRunner {
        platform::windows::WindowsProcessRunner actual;
        fs::path evidence; std::string phase{"discovery"}; std::atomic<unsigned> calls{0},compiles{0};
        explicit Runner(fs::path p):evidence(std::move(p)) {}
        std::expected<process::ProcessResult,process::ProcessError> run(const process::ProcessSpec& spec) override {
            if (phase.starts_with("wave-")) require(++compiles<=24,"real wave process budget");
            const auto prefix=evidence/(std::to_string(++calls)+"-"+phase); log_process(prefix,spec);
            auto r=actual.run(spec); log_result(prefix,r); return r;
        }
    } runner{evidence/"processes"};
    fixture(root);
    msvc::MsvcToolchainLocator locator{runner}; msvc::DiscoveryOptions discovery;
    discovery.preference=msvc::ToolchainPreference::visual_studio;
    discovery.cache_file=root/".mqb/cache/toolchain/vs-x64.cache";
    auto toolchain=locator.discover(discovery);
    if (!toolchain) write(evidence/"discovery.error.txt",toolchain.error().message);
    require(toolchain.has_value(),"discover real MSVC");
    msvc::MsvcModuleDependencyScanner scanner{*toolchain,runner};
    std::vector<modules::ScannedModuleUnit> scanned;
    const auto layout=ProjectArtifactLayout::create(root); require(layout.has_value(),"native module layout");
    CompilerOptions options; options.standard=CppStandard::latest; options.include_directories={root};
    // Only the initial three scans. Later mutations keep provider/import syntax
    // unchanged; this is a compile-wave fixture, not whole-target scan testing.
    for (const auto& filename:{"A.ixx","B.ixx","main.cpp"}) {
        const auto source=root/filename; const auto a=layout->for_source(source); require(a.has_value(),"scan layout");
        runner.phase=std::string{"scan-"}+filename;
        const auto kind=std::string_view{filename}=="main.cpp"?TranslationUnitKind::source:TranslationUnitKind::module_interface;
        auto r=scanner.scan({.source=source,.output_file=a->module_dependencies,.options=options,.kind=kind,.working_directory=root});
        if (!r) write(evidence/(std::string{filename}+".scan-error.txt"),r.error().message);
        require(r && r->dependencies.rules.size()==1,"one real P1689 rule per source");
        write(evidence/(std::string{filename}+".p1689.json"),bytes(a->module_dependencies));
        scanned.push_back({source,r->dependencies.rules[0]});
    }
    auto graph=modules::ModuleDependencyGraphBuilder::build(scanned);
    if (!graph) write(evidence/"graph.error.txt",graph.error().message);
    require(graph && graph->header_units.size()==1 && graph->unresolved_requirements.empty(),"real graph resolves named providers and header unit");
    auto request=request_for(root,*graph);
    msvc::MsvcCompileExecutor executor{*toolchain,runner}; MsvcIncrementalCompileCoordinator compiling{*toolchain,executor};
    MsvcModuleCompileCoordinator wave{compiling}; unsigned calls{};
    auto invoke=[&](const char* phase) {
        require(++calls<=6,"native wave budget"); runner.phase=std::string{"wave-"}+phase;
        save_attempt(evidence,phase,request,"real recorded module compile wave");
        auto r=wave.run_recorded(request,ArtifactGenerationLabel{"native-module",phase}); save_call(evidence,phase,r,true); return r;
    };
    const auto cold=invoke("01-cold"); require(cold.has_value(),"real cold module wave"); check_projection(*cold,request);
    const auto before=snapshot(root/".mqb"); const auto count=runner.calls.load();
    const auto warm=invoke("02-reuse");
    require(warm && !warm->result.any_compiled && count==runner.calls && before==snapshot(root/".mqb"),"real module reuse is read-only");
    require(fs::remove(request.sources[2].artifacts.module_interface),"remove fixture A IFC only");
    const auto repair=invoke("03-ifc-repair");
    require(repair && repair->record.compiles[1].force_rebuild && repair->record.compiles[0].force_rebuild &&
        repair->record.header_unit_compiles[0].completion==ArtifactCompletion::reused,"IFC repair propagates while independent header reuses");
    request.compiler_options.configuration=BuildConfiguration::release;
    const auto release=invoke("04-release");
    require(release && release->result.any_compiled && cold->record.compiles[2].compiler_options.configuration==BuildConfiguration::debug &&
        release->record.compiles[2].compiler_options.configuration==BuildConfiguration::release &&
        cold->record.compiles[2].unit.outputs[0].path==release->record.compiles[2].unit.outputs[0].path,"same-path configuration overwrite keeps prior record independent");
    const auto stamp=fs::last_write_time(root/"A.ixx"); std::this_thread::sleep_for(std::chrono::milliseconds{40});
    write(root/"A.ixx","export module Base;\nexport int module_value(){return 9;}\n");
    require(fs::last_write_time(root/"A.ixx")!=stamp,"observed source change");
    const auto changed=invoke("05-source-changed");
    require(changed && changed->record.compiles[0].force_rebuild,"provider change invalidates transitive consumer");
    // Link the successful wave's real objects before deliberate provider failure.
    msvc::MsvcLinker linker{*toolchain,runner}; MsvcIncrementalLinkCoordinator linking{*toolchain,linker};
    const auto target=layout->for_target("module-record-consumer"); require(target.has_value(),"consumer link layout");
    IncrementalLinkRequest link;
    for (const auto& r:changed->record.compiles) for (const auto& a:r.unit.outputs)
        if (a.kind==ArtifactKind::object) link.objects.push_back(a.path);
    require(link.objects.size()==3,"header unit contributes no guessed link object");
    link.output=target->executable; link.cache_file=target->link_cache; link.working_directory=root;
    link.options.configuration=BuildConfiguration::release; link.options.additional_arguments={"/INCREMENTAL:NO"};
    runner.phase="consumer-link"; auto linked=linking.run(link);
    if (!linked) write(evidence/"consumer-link.error.txt",linked.error().message);
    require(linked.has_value(),"link successful module wave objects");
    runner.phase="program"; process::ProcessSpec program;
    program.executable=link.output; program.working_directory=root; program.capture_stdout=program.capture_stderr=true;
    const auto ran=runner.run(program); require(ran && ran->exit_code==13,"consumer sees changed module and header-unit values");
    const auto failure_stamp=fs::last_write_time(root/"A.ixx"); std::this_thread::sleep_for(std::chrono::milliseconds{40});
    write(root/"A.ixx","export module Base;\n#error MQB_MODULE_RECORD_EXPECTED_FAILURE\n");
    require(fs::last_write_time(root/"A.ixx")!=failure_stamp,"observed deliberate failure input");
    const auto before_failed_wave=runner.compiles.load();
    const auto failed=invoke("06-provider-failed");
    require(!failed && failed.error().code==ModuleCompileErrorCode::compile_failed && failed.error().source==root/"A.ixx" &&
        !fs::exists(evidence/"06-provider-failed.record.txt") && runner.compiles==before_failed_wave+1,
        "failed wave exposes original error and suppresses downstream work");
    require(calls==6,"fixed native wave budget completed");
    write(evidence/"completed.txt","6 waves; 5 successes, 1 expected failure; 3 initial scans; 1 link/1 program exit13 before failure; compiler processes="+
        std::to_string(runner.compiles.load())+"\n");
}
#endif
} // namespace
int main() {
    try {
        model_contracts();
#ifdef _WIN32
        const auto work=fs::current_path();
        require(!fs::exists(work/"storage-fixtures") && !fs::exists(work/"storage-evidence"),"fresh fixture/evidence paths required");
        deterministic_cases(work/"storage-fixtures/module-mock",work/"storage-evidence/module-records/mock");
        native_cases(work/"storage-fixtures/module-native",work/"storage-evidence/module-records/native");
        std::cout<<"module recorded-wave mock and native evidence retained separately\n";
#else
        std::cout<<"portable module record value model only; coordination/MSVC NOT executed\n";
#endif
        return 0;
    } catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
