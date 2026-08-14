#include <atomic>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcLinker.hpp"
#include "mqb/msvc/MsvcModuleDependencyScanner.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"
#include "mqb/orchestration/MsvcModuleCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcModuleTargetCoordinator.hpp"
#include "mqb/process/Process.hpp"

namespace {
namespace fs = std::filesystem;
int failures=0;
void expect(bool condition,std::string_view message){if(!condition){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
class TemporaryDirectory{public:TemporaryDirectory(){const auto t=std::chrono::steady_clock::now().time_since_epoch().count();path_=fs::temp_directory_path()/("mqb-module-target-test-"+std::to_string(t));fs::create_directories(path_);}~TemporaryDirectory(){std::error_code e;fs::remove_all(path_,e);}const fs::path& path()const noexcept{return path_;}private:fs::path path_;};
void write_text(const fs::path& path,std::string_view text){fs::create_directories(path.parent_path());std::ofstream f{path,std::ios::binary|std::ios::trunc};f.write(text.data(),static_cast<std::streamsize>(text.size()));}
std::string path_to_utf8(const fs::path& path){const auto b=path.lexically_normal().generic_u8string();return {reinterpret_cast<const char*>(b.data()),b.size()};}
std::string json_escape(std::string_view value){std::string e;for(char ch:value){switch(ch){case '\\':e+="\\\\";break;case '"':e+="\\\"";break;case '\n':e+="\\n";break;case '\r':e+="\\r";break;case '\t':e+="\\t";break;default:e.push_back(ch);}}return e;}
class ToolchainLikeRunner final:public mqb::process::ProcessRunner{
public:fs::path linker;std::atomic<int> scan_calls{0},compile_calls{0},link_calls{0};std::atomic<bool> emit_two_scan_rules{false};
std::expected<mqb::process::ProcessResult,mqb::process::ProcessError> run(const mqb::process::ProcessSpec& spec)override{if(spec.executable==linker)return run_link(spec);for(const auto& a:spec.arguments)if(a=="/scanDependencies")return run_scan(spec);return run_compile(spec);}
private:
std::expected<mqb::process::ProcessResult,mqb::process::ProcessError> run_scan(const mqb::process::ProcessSpec& spec){++scan_calls;fs::path output,source;for(std::size_t i=0;i<spec.arguments.size();++i)if(spec.arguments[i]=="/scanDependencies"&&i+1<spec.arguments.size())output=fs::path{spec.arguments[i+1]};if(!spec.arguments.empty())source=fs::path{spec.arguments.back()};const bool iface=source.extension()==".ixx";std::string rule=iface?R"json({"provides":[{"logical-name":"math"}]})json":R"json({"requires":[{"logical-name":"math"}]})json";std::string json="{\"version\":1,\"revision\":0,\"rules\":["+rule;if(emit_two_scan_rules.load())json+=",{}";json+="]}";write_text(output,json);return mqb::process::ProcessResult{.exit_code=0};}
std::expected<mqb::process::ProcessResult,mqb::process::ProcessError> run_compile(const mqb::process::ProcessSpec& spec){++compile_calls;fs::path source,object,deps,ifc;for(std::size_t i=0;i<spec.arguments.size();++i){const auto& a=spec.arguments[i];if(a.starts_with("/Fo"))object=fs::path{a.substr(3)};else if(a=="/sourceDependencies"&&i+1<spec.arguments.size())deps=fs::path{spec.arguments[++i]};else if(a=="/ifcOutput"&&i+1<spec.arguments.size())ifc=fs::path{spec.arguments[++i]};}if(!spec.arguments.empty())source=fs::path{spec.arguments.back()};write_text(object,"fake object");if(!ifc.empty())write_text(ifc,"fake ifc");const std::string metadata="{\"Data\":{\"Source\":\""+json_escape(path_to_utf8(source))+"\",\"Includes\":[]}}";write_text(deps,metadata);return mqb::process::ProcessResult{.exit_code=0};}
std::expected<mqb::process::ProcessResult,mqb::process::ProcessError> run_link(const mqb::process::ProcessSpec& spec){++link_calls;fs::path output;for(const auto& a:spec.arguments){constexpr std::string_view p="/OUT:";if(a.starts_with(p)){output=fs::path{a.substr(p.size())};break;}}write_text(output,"fake executable");return mqb::process::ProcessResult{.exit_code=0};}
};
} // namespace

int main(){
TemporaryDirectory fixture;const fs::path root=fixture.path(),module_source=root/"modules/math.ixx",consumer_source=root/"src/main.cpp";write_text(module_source,"export module math;\n");write_text(consumer_source,"import math;\nint main(){return 0;}\n");
auto layout=mqb::ProjectArtifactLayout::create(root);expect(layout.has_value(),"layout");if(!layout)return 1;auto module_artifacts=layout->for_source(module_source),consumer_artifacts=layout->for_source(consumer_source),target_artifacts=layout->for_target("module-app");expect(module_artifacts&&consumer_artifacts&&target_artifacts,"artifacts");if(!module_artifacts||!consumer_artifacts||!target_artifacts)return 1;
const fs::path fake_compiler=root/"toolchain/cl.exe",fake_linker=root/"toolchain/link.exe",fake_librarian=root/"toolchain/lib.exe";write_text(fake_compiler,"fake compiler");write_text(fake_linker,"fake linker");write_text(fake_librarian,"fake librarian");
mqb::msvc::MsvcToolchain toolchain{.identity={.compiler=fake_compiler,.version="19.51.test",.binary_stamp="fake-compiler-stamp"},.linker=fake_linker,.librarian=fake_librarian,.vc_tools_root=fake_compiler.parent_path(),.source=mqb::msvc::ToolchainSource::visual_studio};
ToolchainLikeRunner runner;runner.linker=fake_linker;mqb::msvc::MsvcModuleDependencyScanner scanner{toolchain,runner};mqb::msvc::MsvcCompileExecutor executor{toolchain,runner};mqb::orchestration::MsvcIncrementalCompileCoordinator incremental_compile{toolchain,executor};mqb::orchestration::MsvcModuleCompileCoordinator module_compile{incremental_compile};mqb::msvc::MsvcLinker linker{toolchain,runner};mqb::orchestration::MsvcIncrementalLinkCoordinator incremental_link{toolchain,linker};mqb::orchestration::MsvcModuleTargetCoordinator target{scanner,module_compile,incremental_link};
mqb::orchestration::IncrementalModuleTargetRequest request;request.sources={{.source=consumer_source,.artifacts=*consumer_artifacts,.kind=mqb::TranslationUnitKind::source},{.source=module_source,.artifacts=*module_artifacts,.kind=mqb::TranslationUnitKind::module_interface}};request.target=*target_artifacts;request.compiler_options.standard=mqb::CppStandard::latest;request.link_options.architecture=mqb::Architecture::x64;request.link_options.subsystem=mqb::LinkSubsystem::console;request.working_directory=root;request.max_parallel_scans=2;request.max_parallel_compiles=2;
const auto cold=target.run(request);expect(cold.has_value(),"cold module target should succeed");if(!cold)return 1;expect(cold->scans.size()==2&&cold->scans[0].source==consumer_source&&cold->scans[1].source==module_source,"scan ordering");expect(!cold->scans[0].result.reused&&!cold->scans[1].result.reused,"cold scans must launch compiler");expect(cold->plan.compile_levels.size()==2,"two compile levels");expect(cold->compiles.any_compiled&&cold->link.linked,"cold compile/link");expect(runner.scan_calls.load()==2&&runner.compile_calls.load()==2&&runner.link_calls.load()==1,"cold process counts");
const auto warm=target.run(request);expect(warm.has_value(),"warm target should succeed");if(!warm)return 1;expect(!warm->compiles.any_compiled&&!warm->link.linked,"warm compile/link cache reuse");expect(warm->scans.size()==2&&warm->scans[0].result.reused&&warm->scans[1].result.reused,"warm P1689 scans should reuse sealed evidence");expect(runner.scan_calls.load()==2,"warm target should launch zero additional scans");expect(runner.compile_calls.load()==2&&runner.link_calls.load()==1,"warm target should launch zero compiles/links");
// Force an observable source timestamp change; only the consumer scan should miss.
std::this_thread::sleep_for(std::chrono::milliseconds{20});write_text(consumer_source,"import math;\nint main(){return 0;} // changed\n");
const auto changed=target.run(request);expect(changed.has_value(),"changed consumer should rebuild");if(!changed)return 1;expect(!changed->scans[0].result.reused&&changed->scans[1].result.reused,"source mutation should rescan only changed TU");expect(runner.scan_calls.load()==3,"one changed source should add exactly one raw scan");expect(runner.compile_calls.load()==3,"changed consumer should add exactly one compile");
const auto warm_again=target.run(request);expect(warm_again.has_value(),"re-sealed warm target should succeed");if(!warm_again)return 1;expect(warm_again->scans[0].result.reused&&warm_again->scans[1].result.reused&&runner.scan_calls.load()==3,"successful rebuild should re-seal scan evidence");
// Remove one seal so malformed raw scanner output still exercises fail-closed cardinality validation.
std::error_code ignored;fs::remove(consumer_artifacts->compile_cache,ignored);runner.emit_two_scan_rules=true;const int compiles_before=runner.compile_calls.load(),links_before=runner.link_calls.load();const auto invalid=target.run(request);expect(!invalid&&invalid.error().code==mqb::orchestration::IncrementalModuleTargetErrorCode::invalid_scan_result,"multi-rule raw scan should fail closed");expect(runner.compile_calls.load()==compiles_before&&runner.link_calls.load()==links_before,"invalid scan should stop before compile/link");
if(failures){std::cerr<<failures<<" test(s) failed\n";return 1;}std::cout<<"mqb_module_target_coordinator_tests passed\n";return 0;
}
