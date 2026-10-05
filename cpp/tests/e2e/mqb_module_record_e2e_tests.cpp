#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
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
#ifdef _WIN32
void save_module_cache_evidence(const fs::path&, const RecordedModuleCompileWaveResult&);
void check_module_cache_history(const fs::path&, const RecordedModuleCompileWaveResult&);
std::expected<RecordedModuleCompileWaveResult,ModuleCompileError> run_module_cache_checked(
    MsvcModuleCompileCoordinator&, const ModuleCompileWaveRequest&, ArtifactGenerationLabel, const fs::path&);
#endif
void require(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string{message});
}
std::string text(const fs::path& path) {
    const auto b=path.generic_u8string();
    return {reinterpret_cast<const char*>(b.data()),b.size()};
}
fs::path path_from_utf8(std::string_view value) {
    return fs::path{std::u8string{reinterpret_cast<const char8_t*>(value.data()),value.size()}};
}
// Only the mock's known single-input recipes are recognized here. The fixture
// puts its header in cwd; this is not an implementation of MSVC include search.
fs::path mock_compile_source(const process::ProcessSpec& spec) {
    require(!spec.arguments.empty(),"mock compile arguments must not be empty");
    const bool exports_header=std::find(spec.arguments.begin(),spec.arguments.end(),
        "/exportHeader")!=spec.arguments.end();
    if (!exports_header) return path_from_utf8(spec.arguments.back());
    const auto header=std::find_if(spec.arguments.begin(),spec.arguments.end(),
        [](const std::string& value) { return value=="/headerName:quote" || value=="/headerName:angle"; });
    require(header!=spec.arguments.end() && std::next(header)!=spec.arguments.end(),
        "mock header recipe requires a headerName operand");
    const auto source=path_from_utf8(*std::next(header));
    require(!source.empty() && source.is_relative() && source==fs::path{"extra.hpp"},
        "mock header input must be the declared local extra.hpp fixture");
    require(spec.working_directory && !spec.working_directory->empty(),
        "mock relative header input requires the fixture working directory");
    return (*spec.working_directory/source).lexically_normal();
}
void mock_input_contracts() {
    const auto root=fs::absolute("mock input space");
    process::ProcessSpec spec;
    spec.working_directory=root;
    spec.arguments={"/interface","/sourceDependencies",text(root/"A.json"),text(root/"A.ixx")};
    require(mock_compile_source(spec)==root/"A.ixx","ordinary module input remains the source operand");
    // Mirror the original failing argv: the dependency output is last, while
    // the relative header operand precedes both output switches.
    for (const auto* lookup:{"/headerName:quote","/headerName:angle"}) {
        spec.arguments={"/exportHeader",lookup,"extra.hpp","/ifcOutput",text(root/"extra.ifc"),
            "/sourceDependencies",text(root/"extra.json")};
        require(mock_compile_source(spec)==root/"extra.hpp" &&
            mock_compile_source(spec)!=path_from_utf8(spec.arguments.back()),
            "header input is not the trailing dependency report path");
    }
    auto rejects=[&](const process::ProcessSpec& invalid) {
        bool refused=false;
        try { (void)mock_compile_source(invalid); } catch (const std::runtime_error&) { refused=true; }
        require(refused,"malformed mock source identity must not be guessed");
    };
    auto no_cwd=spec; no_cwd.working_directory.reset(); rejects(no_cwd);
    spec.arguments={"/exportHeader","/headerName:quote"}; rejects(spec);
    spec.arguments={"/exportHeader","/sourceDependencies","extra.json"}; rejects(spec);
    spec.arguments={"/exportHeader","/headerName:quote","other.hpp"}; rejects(spec);
    spec.arguments.clear(); rejects(spec);
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
        std::mutex completion_mutex;
        std::condition_variable completion_changed;
        bool header_completed{false};
        std::vector<std::string> cold_completions;
        explicit Mock(fs::path p):evidence(std::move(p)) {}
        std::expected<process::ProcessResult,process::ProcessError> run(const process::ProcessSpec& spec) override {
            const auto n=++calls; require(n<=16,"mock process budget");
            const auto prefix=evidence/(std::to_string(n)+"-"+phase); log_process(prefix,spec);
            fs::path object,ifc,deps;
            const auto source=mock_compile_source(spec);
            if (phase=="01-cold" && source.filename()=="A.ixx") {
                std::unique_lock lock{completion_mutex};
                require(completion_changed.wait_for(lock,std::chrono::seconds{10},[&]{return header_completed;}),
                    "bounded mock ordering requires the independent header worker");
            }
            write(prefix.string()+".source.txt",text(source));
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
                write(prefix.string()+".sourceDependencies.json",bytes(deps));
            }
            std::expected<process::ProcessResult,process::ProcessError> r=process::ProcessResult{
                .exit_code=failed?2:0,.stdout_text="mock compiler only",.stderr_text=failed?"MODULE_MOCK_FAILURE":""};
            log_result(prefix,r);
            if (phase=="01-cold") {
                std::lock_guard lock{completion_mutex};
                cold_completions.push_back(text(source.filename()));
                if (source.filename()=="extra.hpp") { header_completed=true; completion_changed.notify_all(); }
            }
            return r;
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
        auto r=run_module_cache_checked(wave,input,ArtifactGenerationLabel{"mock-module",phase},evidence/phase);
        save_call(evidence,phase,r,false); if (r) save_module_cache_evidence(evidence/phase,*r); return r;
    };
    const auto cold=invoke("01-cold",request); require(cold.has_value(),"cold mock wave"); check_projection(*cold,request);
    require(cold->record.dependencies.resolved_external_dependencies.size()==1 && runner.calls==4,"external provider is not compiled");
    require(runner.cold_completions==std::vector<std::string>{"extra.hpp","A.ixx","B.ixx","main.cpp"},
        "forced mock completion order is opposite the source request order");
    std::ostringstream completion_log;
    for (const auto& name:runner.cold_completions) completion_log<<name<<'\n';
    write(evidence/"cold-completion-order.txt",completion_log.str());
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
    check_module_cache_history(evidence/"01-cold",*cold);
    check_module_cache_history(evidence/"02-reuse",*reuse);
    for (std::size_t i=0;i<cold->cache_evidence.compiles.size();++i)
        require(bytes((evidence/"01-cold").string()+".source"+std::to_string(i)+".captured.cache") ==
            bytes((evidence/"02-reuse").string()+".source"+std::to_string(i)+".captured.cache"), "mock cold/warm accepted values match");
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
        auto r=run_module_cache_checked(wave,request,ArtifactGenerationLabel{"native-module",phase},evidence/phase);
        save_call(evidence,phase,r,true); if (r) save_module_cache_evidence(evidence/phase,*r); return r;
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
    check_module_cache_history(evidence/"01-cold",*cold);
    check_module_cache_history(evidence/"02-reuse",*warm);
    require(cold->cache_evidence.compiles[0].request.options.configuration==BuildConfiguration::debug &&
        release->cache_evidence.compiles[0].request.options.configuration==BuildConfiguration::release,
        "old cache request survives same-path configuration overwrite and failure");
    require(calls==6,"fixed native wave budget completed");
    write(evidence/"completed.txt","6 waves; 5 successes, 1 expected failure; 3 initial scans; 1 link/1 program exit13 before failure; compiler processes="+
        std::to_string(runner.compiles.load())+"\n");
}
#endif
} // namespace
int main() {
    try {
        model_contracts();
        mock_input_contracts();
#ifdef _WIN32
        const auto work=fs::current_path();
        require(!fs::exists(work/"storage-fixtures") && !fs::exists(work/"storage-evidence"),"fresh fixture/evidence paths required");
        deterministic_cases(work/"storage-fixtures/module-mock",work/"storage-evidence/module-records/mock");
        native_cases(work/"storage-fixtures/module-native",work/"storage-evidence/module-records/native");
        std::cout<<"module recorded-wave mock and native evidence retained separately\n";
#else
        std::cout<<"portable module record and mock-input contracts only; coordination/MSVC NOT executed\n";
#endif
        return 0;
    } catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
// BEGIN MQB_MODULE_CACHE_EVIDENCE_CASES
#ifdef _WIN32
#include "mqb/core/CompileCacheFile.hpp"
#include "mqb/core/PerformanceEvidence.hpp"
namespace {
void save_module_cache_evidence(const fs::path& prefix, const RecordedModuleCompileWaveResult& wave) {
    static_assert(!ModuleCompileArtifactRecord::exact_cache_entry_captured);
    static_assert(!CompileCacheEvidence::producer_identity_verified);
    static_assert(!CompileCacheEvidence::current_content_verified);
    static_assert(!CompileCacheEvidence::complete_producer_inventory);
    static_assert(!CompileCacheEvidence::deletion_authorized);
    auto nodes = [&](const auto& captured, const auto& records, const auto& results, const char* category) {
        require(captured.size() == records.size() && records.size() == results.size(),
            "every successful wave node owns one exact cache value in request order");
        for (std::size_t i=0; i<captured.size(); ++i) {
            const auto& e = captured[i]; const auto& r = records[i];
            require(e.request.unit.source == r.unit.source && e.cache_entry.source == r.unit.source &&
                e.request.unit.kind == r.unit.kind && e.cache_entry.kind == r.unit.kind &&
                e.request.cache_file == r.compile_cache && e.request.source_dependencies_file == r.dependencies &&
                e.request.module_scan_output == r.module_scan_output && e.request.working_directory == r.working_directory &&
                e.request.force_rebuild == r.force_rebuild,
                "captured cache/request belongs to this node and effective provider force");
            require(e.request.options.configuration == r.compiler_options.configuration &&
                e.request.options.standard == r.compiler_options.standard &&
                e.request.options.defines == r.compiler_options.defines &&
                e.request.options.include_directories == r.compiler_options.include_directories &&
                e.request.options.additional_arguments == r.compiler_options.additional_arguments,
                "recorded node retains effective ordered options");
            auto outputs = [&](const auto& values) {
                require(values.size() == r.unit.outputs.size(), "captured output cardinality");
                for (std::size_t j=0; j<values.size(); ++j)
                    require(values[j].path == r.unit.outputs[j].path && values[j].kind == r.unit.outputs[j].kind,
                        "object/IFC ordered output pairing matches original projection");
            };
            outputs(e.request.unit.outputs); outputs(e.cache_entry.outputs);
            require(e.request.unit.module_references.size() == r.unit.module_references.size() &&
                e.request.unit.header_unit_references.size() == r.unit.header_unit_references.size(),
                "captured provider reference cardinality");
            for (std::size_t j=0; j<r.unit.module_references.size(); ++j) {
                const auto& a=e.request.unit.module_references[j]; const auto& b=r.unit.module_references[j];
                require(a.logical_name==b.logical_name && a.interface_file==b.interface_file,
                    "named and external references remain inputs, not extra evidence slots");
            }
            for (std::size_t j=0; j<r.unit.header_unit_references.size(); ++j) {
                const auto& a=e.request.unit.header_unit_references[j]; const auto& b=r.unit.header_unit_references[j];
                require(a.header_name==b.header_name && a.interface_file==b.interface_file && a.lookup_method==b.lookup_method,
                    "header references retain exact identity and order");
            }
            if (r.unit.header_unit) require(e.request.unit.header_unit &&
                e.request.unit.header_unit->header_name == r.unit.header_unit->header_name &&
                e.request.unit.header_unit->lookup_method == r.unit.header_unit->lookup_method &&
                !e.cache_entry.module_scan, "HU value retains header identity and no fabricated scan");
            const auto state = r.cache_state==ArtifactCacheState::reused ? CompileCacheEvidenceState::reused :
                r.cache_state==ArtifactCacheState::saved ? CompileCacheEvidenceState::saved : CompileCacheEvidenceState::save_failed;
            require(e.state==state && (e.state==CompileCacheEvidenceState::reused)==!results[i].result.compiled,
                "typed cache disposition agrees with original completion and warning projection");
            if (e.state==CompileCacheEvidenceState::save_failed) {
                require(e.save_error && e.save_error->code==CompileCacheFileErrorCode::replace_failed &&
                    e.save_error->file==r.compile_cache && e.save_error->offset==0 &&
                    e.save_error->message=="failed to remove previous cache entry",
                    "retain original typed save failure and attempted cache value");
                require(bytes(r.compile_cache/"sentinel")=="retain obstruction", "save failure preserves sentinel");
            } else require(!e.save_error, "saved/reused evidence has no invented save error");
            const auto stem=prefix.string()+"."+category+std::to_string(i);
            require(CompileCacheFile::save(stem+".captured.cache",e.cache_entry).has_value(),
                "test-only serialization of owning cache with unchanged codec");
            if (e.state!=CompileCacheEvidenceState::save_failed)
                require(bytes(stem+".captured.cache")==bytes(r.compile_cache), "same-call cache bytes match disk copy");
            std::ostringstream out;
            out<<"attached_exact_cache_entry_captured=true\nlegacy_projection_exact_cache_entry_captured=false\n"
                <<"state="<<static_cast<int>(e.state)<<"\nsource="<<text(e.request.unit.source)
                <<"\nforce_rebuild="<<e.request.force_rebuild<<"\nconfiguration="<<to_string(e.request.options.configuration)
                <<"\nsave_error="<<e.save_error.has_value()<<"\nproducer_identity_verified=false\ncurrent_content_verified=false\n"
                <<"complete_producer_inventory=false\ndeletion_authorized=false\n";
            if (e.save_error) out<<"save_code="<<static_cast<int>(e.save_error->code)
                <<"\nsave_path="<<text(e.save_error->file)<<"\nsave_offset="<<e.save_error->offset
                <<"\nsave_message="<<e.save_error->message<<'\n';
            write(stem+".cache-evidence.txt",out.str());
            // Value ownership control, not a new compiler/cache operation.
            const auto source=e.request.unit.source;
            const auto environments=e.inspection_toolchain.environment.size();
            auto copy=e;
            copy.request.unit.source="changed-copy.cpp";
            copy.cache_entry.outputs.clear();
            copy.inspection_toolchain.environment.push_back({"SYNTHETIC_ONLY","not serialized"});
            require(e.request.unit.source==source && e.cache_entry.outputs.size()==r.unit.outputs.size() &&
                e.inspection_toolchain.environment.size()==environments, "cache/request/toolchain vectors own their values");
        }
    };
    nodes(wave.cache_evidence.compiles,wave.record.compiles,wave.result.compiles,"source");
    nodes(wave.cache_evidence.header_unit_compiles,wave.record.header_unit_compiles,wave.result.header_unit_compiles,"header");
}
void check_module_cache_history(const fs::path& prefix, const RecordedModuleCompileWaveResult& wave) {
    auto nodes=[&](const auto& values,const char* category) {
        for (std::size_t i=0;i<values.size();++i) {
            const auto stem=prefix.string()+"."+category+std::to_string(i);
            require(CompileCacheFile::save(stem+".history.cache",values[i].cache_entry).has_value(),"serialize old module value after later failures");
            require(bytes(stem+".history.cache")==bytes(stem+".captured.cache"), "later overwrite/failure cannot mutate earlier module cache evidence");
        }
    };
    nodes(wave.cache_evidence.compiles,"source"); nodes(wave.cache_evidence.header_unit_compiles,"header");
}
std::expected<RecordedModuleCompileWaveResult,ModuleCompileError> run_module_cache_checked(
    MsvcModuleCompileCoordinator& wave,const ModuleCompileWaveRequest& request,
    ArtifactGenerationLabel label,const fs::path& prefix) {
    performance::Collector collector;
    auto result=[&]() {
        performance::Activation active{collector};
        return wave.run_recorded(request,std::move(label));
    }();
    const auto count=collector.snapshot();
    constexpr auto ci=static_cast<std::size_t>(performance::CacheKind::compile);
    const auto limit=request.sources.size()+request.header_units.size();
    require(count.cache_files_opened[ci]<=limit && count.cache_files_written[ci]<=limit,
        "recording adds no cache open/save beyond one original lower call per node");
    if (prefix.filename()=="02-reuse") require(result && count.cache_files_opened[ci]==limit &&
        count.cache_files_written[ci]==0, "warm wave exactly one accepted cache payload per node and zero saves");
    std::ostringstream out;
    out<<"compile_cache_reads="<<count.cache_files_opened[ci]<<"\ncompile_cache_writes="<<count.cache_files_written[ci]
        <<"\nmaximum_original_payload_opens="<<limit<<'\n';
    write(prefix.string()+".cache-counts.txt",out.str());
    return result;
}
} // namespace
#endif
// END MQB_MODULE_CACHE_EVIDENCE_CASES
