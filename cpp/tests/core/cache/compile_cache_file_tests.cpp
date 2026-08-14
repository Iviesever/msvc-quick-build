#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string_view>
#include <vector>

#include "mqb/core/Artifact.hpp"
#include "mqb/core/BuildSignature.hpp"
#include "mqb/core/CompileCache.hpp"
#include "mqb/core/CompileCacheFile.hpp"
#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/ToolchainIdentity.hpp"
#include "mqb/core/TranslationUnit.hpp"

namespace {
namespace fs = std::filesystem;
int failures = 0;
void expect(const bool condition, const std::string_view message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() / ("mqb-cache-file-test-" + std::to_string(tick));
        fs::create_directories(path_);
    }
    ~TemporaryDirectory() { std::error_code ignored; fs::remove_all(path_, ignored); }
    [[nodiscard]] const fs::path& path() const noexcept { return path_; }
private: fs::path path_;
};
mqb::ToolchainIdentity make_toolchain(const std::string_view version) {
    return {.compiler="toolchain/cl.exe", .version=std::string{version}, .binary_stamp="size=123;mtime=456"};
}
mqb::CompilerOptions make_options() {
    mqb::CompilerOptions options;
    options.configuration=mqb::BuildConfiguration::debug; options.architecture=mqb::Architecture::x64;
    options.standard=mqb::CppStandard::cpp23; options.defines={"FEATURE=1"}; options.include_directories={"include"};
    return options;
}
mqb::FileSnapshot snapshot(const fs::path& path, const std::int64_t ticks) {
    return {.path=path,.exists=true,.modified=fs::file_time_type{
        std::chrono::duration_cast<fs::file_time_type::duration>(std::chrono::nanoseconds{ticks})}};
}
bool same_snapshot(const mqb::FileSnapshot& a,const mqb::FileSnapshot& b) {
    return a.path==b.path && a.exists==b.exists && a.modified==b.modified;
}
mqb::CompileCacheEntry make_entry(const std::string_view version="19.50.10000") {
    mqb::TranslationUnit unit; unit.source="src/main file.cpp"; unit.kind=mqb::TranslationUnitKind::source;
    unit.outputs={mqb::Artifact{"cache/main file.obj",mqb::ArtifactKind::object}};
    const auto toolchain=make_toolchain(version); const auto options=make_options();
    return {.source=unit.source,.kind=unit.kind,.toolchain=toolchain,
        .signature=mqb::BuildSignature::for_compile(unit,toolchain,options),.outputs=unit.outputs,
        .dependencies={fs::path{"src/main file.cpp"},fs::path{"include/a.hpp"},fs::path{"include/nested/b.hpp"}}};
}
mqb::CompileCacheEntry make_module_entry(const bool ifc_only) {
    mqb::TranslationUnit unit; unit.source=ifc_only?fs::path{"include/value.hpp"}:fs::path{"src/math.ixx"};
    unit.kind=mqb::TranslationUnitKind::module_interface;
    if(!ifc_only) unit.outputs.push_back({.path="cache/math.obj",.kind=mqb::ArtifactKind::object});
    unit.outputs.push_back({.path=ifc_only?fs::path{"ifc/value.ifc"}:fs::path{"ifc/math.ifc"},.kind=mqb::ArtifactKind::module_interface});
    const auto toolchain=make_toolchain("19.51.modules"); const auto options=make_options();
    mqb::CompileCacheEntry entry{.source=unit.source,.kind=unit.kind,.toolchain=toolchain,
        .signature=mqb::BuildSignature::for_compile(unit,toolchain,options),.outputs=unit.outputs,
        .dependencies={fs::path{"include/shared.hpp"}}};
    if(!ifc_only) entry.module_scan=mqb::ModuleScanEvidence{
        .signature=mqb::BuildSignature::for_module_scan(unit.source,unit.kind,toolchain,options),
        .source=snapshot(unit.source,100),.output=snapshot("scan/math.json",200),
        .dependencies={snapshot("include/shared.hpp",150),snapshot("include/config.hpp",175)}};
    return entry;
}
void write_bytes(const fs::path& file,const std::initializer_list<std::uint8_t> bytes) {
    std::ofstream stream{file,std::ios::binary|std::ios::trunc}; for(const auto byte:bytes) stream.put(static_cast<char>(byte));
}
std::vector<char> read_all(const fs::path& file) {
    std::ifstream stream{file,std::ios::binary}; return {std::istreambuf_iterator<char>{stream},std::istreambuf_iterator<char>{}};
}
void write_all(const fs::path& file,const std::vector<char>& bytes) {
    std::ofstream stream{file,std::ios::binary|std::ios::trunc}; stream.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
}
void expect_round_trip(const fs::path& file,const mqb::CompileCacheEntry& original,const std::string_view description) {
    expect(mqb::CompileCacheFile::save(file,original).has_value(),std::string{description}+" should save");
    const auto loaded=mqb::CompileCacheFile::load(file); expect(loaded&&*loaded,std::string{description}+" should load"); if(!loaded||!*loaded)return;
    const auto& entry=**loaded;
    expect(entry.source==original.source,std::string{description}+" source should round-trip");
    expect(entry.kind==original.kind,std::string{description}+" TU kind should round-trip");
    expect(entry.toolchain.compiler==original.toolchain.compiler && entry.toolchain.version==original.toolchain.version
        && entry.toolchain.binary_stamp==original.toolchain.binary_stamp,std::string{description}+" toolchain should round-trip");
    expect(entry.signature==original.signature,std::string{description}+" signature should round-trip");
    expect(entry.outputs.size()==original.outputs.size(),std::string{description}+" output count should round-trip");
    if(entry.outputs.size()==original.outputs.size()) for(std::size_t i=0;i<entry.outputs.size();++i)
        expect(entry.outputs[i].path==original.outputs[i].path && entry.outputs[i].kind==original.outputs[i].kind,"output should round-trip");
    expect(entry.dependencies==original.dependencies,std::string{description}+" dependencies should round-trip");
    expect(entry.module_scan.has_value()==original.module_scan.has_value(),std::string{description}+" scan evidence presence should round-trip");
    if(entry.module_scan&&original.module_scan){
        expect(entry.module_scan->signature==original.module_scan->signature,"scan signature should round-trip");
        expect(same_snapshot(entry.module_scan->source,original.module_scan->source),"scan source snapshot should round-trip");
        expect(same_snapshot(entry.module_scan->output,original.module_scan->output),"scan output snapshot should round-trip");
        expect(entry.module_scan->dependencies.size()==original.module_scan->dependencies.size(),"scan dependency count should round-trip");
        if(entry.module_scan->dependencies.size()==original.module_scan->dependencies.size()) for(std::size_t i=0;i<entry.module_scan->dependencies.size();++i)
            expect(same_snapshot(entry.module_scan->dependencies[i],original.module_scan->dependencies[i]),"scan dependency snapshot should round-trip");
    }
}
} // namespace
int main(){
    TemporaryDirectory fixture; const fs::path file=fixture.path()/"nested"/"main.mqbcache";
    const auto missing=mqb::CompileCacheFile::load(file); expect(missing&&!missing->has_value(),"missing cache file should be a normal miss");
    const auto original=make_entry(); expect_round_trip(file,original,"ordinary object-only cache entry"); expect(fs::is_regular_file(file),"save should install final cache file");
    const auto module_entry=make_module_entry(false); const fs::path module_file=fixture.path()/"module.mqbcache";
    expect_round_trip(module_file,module_entry,"module object+IFC cache entry with scan evidence");
    expect_round_trip(fixture.path()/"ifc-only.mqbcache",make_module_entry(true),"IFC-only cache entry");
    const fs::path v2_file=fixture.path()/"legacy-v2.mqbcache"; expect(mqb::CompileCacheFile::save(v2_file,original).has_value(),"v2 fixture should start as v3");
    auto v2_bytes=read_all(v2_file); expect(v2_bytes.size()>13,"v2 fixture should contain payload and presence marker");
    if(v2_bytes.size()>13){v2_bytes[8]=2;v2_bytes[9]=0;v2_bytes[10]=0;v2_bytes[11]=0;v2_bytes.pop_back();write_all(v2_file,v2_bytes);
        const auto v2=mqb::CompileCacheFile::load(v2_file);expect(v2&&*v2&&!(**v2).module_scan,"legacy v2 should load without scan evidence");}
    auto empty_outputs=original;empty_outputs.outputs.clear();expect(!mqb::CompileCacheFile::save(fixture.path()/"empty.mqbcache",empty_outputs),"empty outputs should be rejected");
    const auto replacement=make_entry("19.51.20000");expect(mqb::CompileCacheFile::save(file,replacement).has_value(),"replacement should save");
    const auto reloaded=mqb::CompileCacheFile::load(file);expect(reloaded&&*reloaded&&(**reloaded).toolchain.version=="19.51.20000","replacement should not leave stale metadata");
    const fs::path bad=fixture.path()/"bad.mqbcache";write_bytes(bad,{0,1,2,3,4,5,6,7,3,0,0,0});const auto bad_result=mqb::CompileCacheFile::load(bad);
    expect(!bad_result,"bad magic should fail");if(!bad_result)expect(bad_result.error().code==mqb::CompileCacheFileErrorCode::invalid_magic,"bad magic code");
    const fs::path v1=fixture.path()/"v1.mqbcache";write_bytes(v1,{'M','Q','B','C','A','C','H','E',1,0,0,0});const auto v1r=mqb::CompileCacheFile::load(v1);
    expect(!v1r,"v1 should fail");if(!v1r)expect(v1r.error().code==mqb::CompileCacheFileErrorCode::unsupported_version,"v1 code");
    const fs::path future=fixture.path()/"future.mqbcache";write_bytes(future,{'M','Q','B','C','A','C','H','E',4,0,0,0});const auto fr=mqb::CompileCacheFile::load(future);
    expect(!fr,"future version should fail");if(!fr)expect(fr.error().code==mqb::CompileCacheFileErrorCode::unsupported_version,"future code");
    const fs::path truncated=fixture.path()/"truncated.mqbcache";expect(mqb::CompileCacheFile::save(truncated,module_entry).has_value(),"truncation fixture valid");fs::resize_file(truncated,20);expect(!mqb::CompileCacheFile::load(truncated),"truncated file should fail");
    const fs::path trailing=fixture.path()/"trailing.mqbcache";expect(mqb::CompileCacheFile::save(trailing,original).has_value(),"trailing fixture valid");{std::ofstream s{trailing,std::ios::binary|std::ios::app};s.put(static_cast<char>(0x7f));}
    const auto tr=mqb::CompileCacheFile::load(trailing);expect(!tr,"trailing bytes should fail");if(!tr)expect(tr.error().code==mqb::CompileCacheFileErrorCode::corrupt_data,"trailing code");
    if(failures){std::cerr<<failures<<" test(s) failed\n";return 1;}std::cout<<"mqb_compile_cache_file_tests passed\n";return 0;
}
