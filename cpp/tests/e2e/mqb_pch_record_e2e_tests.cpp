#include <algorithm>
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

#include "mqb/core/BuildArtifactRecord.hpp"
#ifdef _WIN32
#include "mqb/orchestration/MsvcIncrementalPchCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalTargetCoordinator.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"
#endif

namespace {
namespace fs = std::filesystem;
using namespace mqb;
void require(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string{message});
}
void model_contracts() {
    static_assert(!PchArtifactRecord::deletion_authorized);
    static_assert(!PchArtifactRecord::physical_identity_verified);
    static_assert(!PchArtifactRecord::exact_cache_entry_captured);
    static_assert(!PchArtifactRecord::complete_producer_inventory);
    PchArtifactRecord first{
        .caller_label=ArtifactGenerationLabel{"target", "old"},
        .completion=ArtifactCompletion::executed, .cache_state=ArtifactCacheState::saved,
        .input_header="input.hpp", .creator={.source="creator.cpp",
            .outputs={{"creator.obj", ArtifactKind::object}, {"project.pch", ArtifactKind::precompiled_header}}},
        .compiler_options={.precompiled_header=PrecompiledHeaderBinding{
            "input.hpp", "project.pch", PrecompiledHeaderRole::create}},
        .dependencies="creator.deps.json", .compile_cache="creator.cache", .working_directory="project",
        .creator_source_materialization_required=true};
    auto second = first;
    second.caller_label->generation = "new";
    second.compiler_options.configuration = BuildConfiguration::release;
    second.creator.outputs[0].path = "changed.obj";
    second.completion = ArtifactCompletion::reused;
    require(first.caller_label->generation == "old" && first.creator.outputs[0].path == "creator.obj" &&
        first.compiler_options.configuration == BuildConfiguration::debug, "PCH record owns its data");
    require(first.input_header != first.creator.source && first.creator.outputs.size() == 2,
        "header input, synthetic creator and paired outputs remain distinct");
}
#ifdef _WIN32
using namespace mqb::orchestration;
std::string text(const fs::path& path) {
    const auto b = path.generic_u8string();
    return {reinterpret_cast<const char*>(b.data()), b.size()};
}
fs::path path_from_utf8(std::string_view value) {
    return fs::path{std::u8string{reinterpret_cast<const char8_t*>(value.data()), value.size()}};
}
void write(const fs::path& path, std::string_view value) {
    fs::create_directories(path.parent_path());
    std::ofstream out{path, std::ios::binary};
    out.write(value.data(), static_cast<std::streamsize>(value.size()));
    out.close();
    require(static_cast<bool>(out), "write PCH fixture/evidence");
}
std::string bytes(const fs::path& path) {
    std::ifstream in{path, std::ios::binary};
    require(in.is_open(), "open PCH fixture snapshot");
    std::string result{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    require(!in.bad(), "read PCH fixture snapshot");
    return result;
}
struct FileState {
    fs::file_time_type modified;
    std::string contents;
    bool operator==(const FileState&) const = default;
};
std::map<std::string, FileState> snapshot(const fs::path& root) {
    std::map<std::string, FileState> result;
    if (fs::exists(root)) for (const auto& e : fs::recursive_directory_iterator(root)) {
        if (e.is_regular_file()) result.emplace(text(e.path().lexically_relative(root)),
            FileState{e.last_write_time(), bytes(e.path())});
    }
    return result;
}
std::string describe(const RecordedPchResult& value) {
    const auto& r = value.record;
    std::ostringstream out;
    out << "completion=" << (r.completion == ArtifactCompletion::executed ? "executed" : "reused")
        << "\ncache_state=" << (r.cache_state == ArtifactCacheState::saved ? "saved" :
            r.cache_state == ArtifactCacheState::reused ? "reused" : "save_failed")
        << "\ninput_header=" << text(r.input_header) << "\ncreator=" << text(r.creator.source)
        << "\nconfiguration=" << to_string(r.compiler_options.configuration)
        << "\narchitecture=" << to_string(r.compiler_options.architecture)
        << "\ncreator_role=" << (r.compiler_options.precompiled_header->role == PrecompiledHeaderRole::create ? "create" : "use")
        << "\nbound_header=" << text(r.compiler_options.precompiled_header->header)
        << "\nbound_pch=" << text(r.compiler_options.precompiled_header->artifact)
        << "\ndependencies=" << text(r.dependencies) << "\ncompile_cache=" << text(r.compile_cache)
        << "\nmaterialization_required=" << r.creator_source_materialization_required
        << "\ncompiled=" << value.result.compile.compiled
        << "\nexact_cache_entry_captured=false\nphysical_identity_verified=false\ndelete_authorized=false\n";
    if (r.working_directory) out << "cwd=" << text(*r.working_directory) << '\n';
    if (r.caller_label) out << "caller_generation=" << r.caller_label->generation << '\n';
    for (const auto& a : r.creator.outputs) out << "output=" << text(a.path) << '\n';
    for (const auto& reason : value.result.compile.validation.reasons) out << "reason=" << to_string(reason) << '\n';
    for (const auto& warning : value.result.compile.warnings) out << "warning_code=" << static_cast<int>(warning.code)
        << "\nwarning_path=" << text(warning.path) << "\nwarning=" << warning.message << '\n';
    return out.str();
}
void log_process(const fs::path& prefix, const process::ProcessSpec& spec) {
    std::ostringstream out;
    out << "executable=" << text(spec.executable) << '\n';
    if (spec.working_directory) out << "cwd=" << text(*spec.working_directory) << '\n';
    for (const auto& a : spec.arguments) out << "arg=" << a << '\n';
    write(prefix.string()+".argv.txt", out.str());
}
void log_result(const fs::path& prefix, const std::expected<process::ProcessResult, process::ProcessError>& r) {
    if (!r) write(prefix.string()+".launch-error.txt", r.error().message);
    else {
        write(prefix.string()+".exit.txt", std::to_string(r->exit_code));
        write(prefix.string()+".stdout.txt", r->stdout_text);
        write(prefix.string()+".stderr.txt", r->stderr_text);
    }
}
void save_pch_cache_evidence(const fs::path&, const RecordedPchResult&);
void check_pch_cache_history(const fs::path&, const RecordedPchResult&);
std::expected<RecordedPchResult, IncrementalPchError> run_pch_cache_checked(
    MsvcIncrementalPchCoordinator&, const IncrementalPchRequest&,
    ArtifactGenerationLabel, const fs::path&);
void save_call(const fs::path& evidence, const char* phase,
               const std::expected<RecordedPchResult, IncrementalPchError>& r, bool copy_artifacts) {
    const auto prefix = evidence / phase;
    if (!r) {
        std::ostringstream out;
        out << "code=" << static_cast<int>(r.error().code) << "\nmessage=" << r.error().message << '\n';
        if (r.error().compile_error) {
            out << "compile_code=" << static_cast<int>(r.error().compile_error->code)
                << "\ncompile_message=" << r.error().compile_error->message << '\n';
            if (r.error().compile_error->compile_error) out << "executor_code="
                << static_cast<int>(r.error().compile_error->compile_error->code)
                << "\nexecutor_message=" << r.error().compile_error->compile_error->message << '\n';
        }
        write(prefix.string()+".error.txt", out.str());
        return;
    }
    write(prefix.string()+".record.txt", describe(*r));
    save_pch_cache_evidence(prefix, *r);
    if (copy_artifacts) {
        // Test-only evidence outside .mqb; never a product persistence format.
        write(prefix.string()+".creator.cpp", bytes(r->record.creator.source));
        for (const auto& a : r->record.creator.outputs)
            write(prefix.string()+(a.kind == ArtifactKind::object ? ".obj" : ".pch"), bytes(a.path));
        write(prefix.string()+".deps.json", bytes(r->record.dependencies));
        if (r->record.cache_state != ArtifactCacheState::save_failed)
            write(prefix.string()+".compilecache", bytes(r->record.compile_cache));
    }
}
IncrementalPchRequest request_for(const fs::path& root) {
    auto layout = ProjectArtifactLayout::create(root);
    require(layout.has_value(), "PCH layout");
    auto artifacts = layout->for_precompiled_header("recorded-pch", BuildConfiguration::debug, Architecture::x64);
    require(artifacts.has_value(), "PCH artifact paths");
    IncrementalPchRequest r;
    r.header = root / "include/input.hpp";
    r.artifacts = *artifacts;
    r.working_directory = root;
    return r;
}
void corrupt_creator_same_time(const fs::path& path) {
    const auto stamp = fs::last_write_time(path);
    write(path, "// intentionally stale creator; retain original timestamp\n");
    fs::last_write_time(path, stamp);
    require(fs::last_write_time(path) == stamp, "fixture preserves creator mtime");
}
void check_repair(const std::expected<RecordedPchResult, IncrementalPchError>& r) {
    require(r && r->result.compile.compiled && r->record.creator_source_materialization_required,
        "same-timestamp creator repaired without false reuse");
    const auto& reasons = r->result.compile.validation.reasons;
    require(std::find(reasons.begin(), reasons.end(), BuildReason::source_changed) != reasons.end(),
        "repair retains semantic source_changed diagnosis");
}
// Only emits the small sourceDependencies fixture consumed by the real decoder.
std::string json_string(std::string_view value) {
    std::string result{"\""};
    for (char c : value) { if (c == '\\' || c == '"') result += '\\'; result += c; }
    return result+'"';
}
void deterministic_cases(const fs::path& root, const fs::path& evidence) {
    struct Mock final : process::ProcessRunner {
        fs::path evidence;
        std::string phase;
        unsigned calls{};
        bool fail{false}, omit_pch{false};
        explicit Mock(fs::path p) : evidence(std::move(p)) {}
        std::expected<process::ProcessResult, process::ProcessError> run(const process::ProcessSpec& spec) override {
            require(++calls <= 6, "mock compiler process budget");
            const auto prefix = evidence / (std::to_string(calls)+"-"+phase);
            log_process(prefix, spec);
            fs::path object, pch, source, dependencies, header;
            for (std::size_t i=0; i<spec.arguments.size(); ++i) {
                const auto& a = spec.arguments[i];
                if (a.starts_with("/Fo")) object = path_from_utf8(a.substr(3));
                else if (a.starts_with("/Fp")) pch = path_from_utf8(a.substr(3));
                else if (a.starts_with("/FI")) header = path_from_utf8(a.substr(3));
                else if (a == "/sourceDependencies" && i+1<spec.arguments.size()) dependencies = path_from_utf8(spec.arguments[++i]);
                else if (a.ends_with(".cpp")) source = path_from_utf8(a);
            }
            require(!object.empty() && !pch.empty() && !source.empty() && !dependencies.empty() && !header.empty(),
                "mock receives actual compiler-owned PCH arguments");
            if (!fail) {
                write(object, "mock creator object");
                if (!omit_pch) write(pch, "mock precompiled header");
                write(dependencies, "{\"Version\":\"1.2\",\"Data\":{\"Source\":"+json_string(text(source))+
                    ",\"Includes\":["+json_string(text(header))+"]}}");
            }
            std::expected<process::ProcessResult, process::ProcessError> r = process::ProcessResult{
                .exit_code=fail ? 2 : 0, .stdout_text="mock cl result", .stderr_text=fail ? "MOCK_PCH_FAILURE" : ""};
            log_result(prefix, r);
            return r;
        }
    } runner{evidence / "processes"};
    write(root / "tools/cl.exe", "mock compiler identity");
    auto request = request_for(root);
    write(request.header, "#pragma once\ninline int value(){return 7;}\n");
    msvc::MsvcToolchain toolchain{.identity={.compiler=root/"tools/cl.exe", .version="pch-record-mock", .binary_stamp="fixture"}};
    toolchain.environment = {{"MQB_PCH_OWNERSHIP", "first"}, {"MQB_PCH_OWNERSHIP", "second"}};
    msvc::MsvcCompileExecutor executor{toolchain, runner};
    MsvcIncrementalCompileCoordinator compiling{toolchain, executor};
    MsvcIncrementalPchCoordinator pch{compiling};
    unsigned calls{};
    auto invoke = [&](const char* phase, const IncrementalPchRequest& input) {
        require(++calls <= 10, "mock PCH API budget"); runner.phase = phase;
        write(evidence/(std::string{phase}+".attempt.txt"), "mock cl; real PCH/compile/cache pipeline\n");
        auto r = run_pch_cache_checked(pch, input, ArtifactGenerationLabel{"mock-pch",phase}, evidence/phase);
        save_call(evidence, phase, r, false); return r;
    };
    // The PCH coordinator replaces a caller's use binding with its own creator
    // binding. The record must carry the actual projection, not this input.
    request.compiler_options.precompiled_header = PrecompiledHeaderBinding{
        root/"wrong.hpp", root/"wrong.pch", PrecompiledHeaderRole::use};
    const auto cold = invoke("01-cold", request);
    require(cold && cold->record.completion == ArtifactCompletion::executed && cold->record.cache_state == ArtifactCacheState::saved &&
        cold->record.compiler_options.precompiled_header->role == PrecompiledHeaderRole::create &&
        cold->record.input_header == request.header && cold->record.creator.outputs.size() == 2, "actual creator projection recorded");
    const auto before = snapshot(root/".mqb");
    const auto warm = invoke("02-reuse", request);
    require(warm && warm->record.completion == ArtifactCompletion::reused && warm->record.cache_state == ArtifactCacheState::reused &&
        !warm->record.creator_source_materialization_required && runner.calls == 1 && before == snapshot(root/".mqb"), "PCH reuse is read-only");
    corrupt_creator_same_time(request.artifacts.source);
    check_repair(invoke("03-creator-repair", request));
    require(fs::remove(request.artifacts.precompiled_header), "remove only fixture PCH for repair control");
    const auto missing = invoke("04-missing-pch", request);
    require(missing && missing->record.completion == ArtifactCompletion::executed, "missing PCH is rebuilt, not reused");
    auto blocked_cache = request;
    blocked_cache.artifacts.compile_cache = root/".mqb/blocked-cache";
    write(blocked_cache.artifacts.compile_cache/"sentinel", "retain obstruction");
    const auto save_failed = invoke("05-save-failed", blocked_cache);
    require(save_failed && save_failed->record.cache_state == ArtifactCacheState::save_failed &&
        !save_failed->result.compile.warnings.empty(), "cache warning is not durable success");
    auto failure = request; failure.compiler_options.defines.push_back("PCH_MOCK_FAILURE");
    runner.fail = true;
    const auto tool_failed = invoke("06-tool-failed", failure);
    require(!tool_failed && tool_failed.error().compile_error.has_value(), "original compiler failure preserved");
    runner.fail = false; runner.omit_pch = true;
    require(fs::remove(request.artifacts.precompiled_header), "fresh missing-output control");
    const auto omitted = invoke("07-no-pch", failure);
    require(!omitted && omitted.error().compile_error && omitted.error().compile_error->compile_error &&
        omitted.error().compile_error->compile_error->code == msvc::CompileExecutorErrorCode::output_missing, "successful mock without PCH is rejected");
    runner.omit_pch = false;
    auto blocked_creator = request;
    blocked_creator.artifacts.source = root/".mqb/blocked.cpp";
    write(blocked_creator.artifacts.source/"sentinel", "retain creator obstruction");
    const auto blocked = invoke("08-blocked-creator", blocked_creator);
    require(!blocked && blocked.error().code == IncrementalPchErrorCode::synthetic_source_failed, "creator write failure is not completion");
    auto no_header = request; no_header.header = root/"absent.hpp";
    const auto absent = invoke("09-no-header", no_header);
    require(!absent && absent.error().code == IncrementalPchErrorCode::header_missing, "missing input is rejected");
    auto invalid = request; invalid.artifacts.object.clear();
    const auto bad = invoke("10-invalid", invalid);
    require(!bad && bad.error().code == IncrementalPchErrorCode::invalid_request, "empty output rejected");
    toolchain.environment[0].value = "changed after failures";
    require(cold->cache_evidence.inspection_toolchain.environment.size() == 2 &&
        cold->cache_evidence.inspection_toolchain.environment[0].value == "first" &&
        cold->cache_evidence.inspection_toolchain.environment[1].value == "second",
        "owning environment preserves order and duplicate names without serialization");
    check_pch_cache_history(evidence/"01-cold", *cold);
    check_pch_cache_history(evidence/"02-reuse", *warm);
    require(calls == 10 && runner.calls == 6, "mock fixed budget completed");
    write(evidence/"completed.txt", "10 PCH API calls; 5 success, 5 expected errors; 6 mock processes; no retries\n");
}
void native_cases(const fs::path& root, const fs::path& evidence) {
    struct Runner final : process::ProcessRunner {
        platform::windows::WindowsProcessRunner actual;
        fs::path evidence;
        std::string phase{"discovery"};
        unsigned calls{};
        explicit Runner(fs::path p) : evidence(std::move(p)) {}
        std::expected<process::ProcessResult, process::ProcessError> run(const process::ProcessSpec& spec) override {
            const auto prefix = evidence/(std::to_string(++calls)+"-"+phase);
            log_process(prefix,spec); auto r = actual.run(spec); log_result(prefix,r); return r;
        }
    } runner{evidence/"processes"};
    fs::create_directories(root);
    msvc::MsvcToolchainLocator locator{runner};
    msvc::DiscoveryOptions discovery;
    discovery.preference = msvc::ToolchainPreference::visual_studio;
    discovery.cache_file = root/".mqb/cache/toolchain/vs-x64.cache";
    auto toolchain = locator.discover(discovery);
    if (!toolchain) write(evidence/"discovery.error.txt",toolchain.error().message);
    require(toolchain.has_value(), "discover MSVC for PCH producer");
    msvc::MsvcCompileExecutor executor{*toolchain,runner};
    MsvcIncrementalCompileCoordinator compiling{*toolchain,executor};
    MsvcIncrementalPchCoordinator pch{compiling};
    auto request = request_for(root);
    write(request.header,"#pragma once\ninline int pch_value(){return 7;}\n");
    unsigned calls{};
    auto invoke = [&](const char* phase) {
        require(++calls <= 6, "native PCH API budget"); runner.phase=phase;
        write(evidence/(std::string{phase}+".attempt.txt"),"real PCH recorded API invocation\n");
        auto r=run_pch_cache_checked(pch,request,ArtifactGenerationLabel{"native-pch",phase},evidence/phase);
        save_call(evidence,phase,r,true); return r;
    };
    const auto cold=invoke("01-cold");
    require(cold && cold->record.completion==ArtifactCompletion::executed && cold->record.cache_state==ArtifactCacheState::saved,
        "native PCH creation recorded");
    const auto before=snapshot(root/".mqb"); const auto processes=runner.calls;
    const auto reuse=invoke("02-reuse");
    require(reuse && reuse->record.completion==ArtifactCompletion::reused && runner.calls==processes && before==snapshot(root/".mqb"),
        "native reuse neither launches cl nor rewrites PCH state");
    corrupt_creator_same_time(request.artifacts.source);
    check_repair(invoke("03-creator-repair"));
    request.compiler_options.configuration=BuildConfiguration::release;
    const auto release=invoke("04-release");
    require(release && release->result.compile.compiled && cold->record.compiler_options.configuration==BuildConfiguration::debug &&
        release->record.compiler_options.configuration==BuildConfiguration::release &&
        release->record.creator.outputs.front().path==cold->record.creator.outputs.front().path,
        "same paths, different compiler configuration; old record not mutated");
    const auto header_stamp=fs::last_write_time(request.header);
    std::this_thread::sleep_for(std::chrono::milliseconds{40});
    write(request.header,"#pragma once\ninline int pch_value(){return 11;}\n");
    require(fs::last_write_time(request.header) != header_stamp, "native header timestamp change observed");
    const auto changed=invoke("05-header-changed");
    require(changed && changed->result.compile.compiled, "changed input header rebuilds PCH");
    // Consumer executes before the deliberately failed PCH. A failure is not a
    // rollback promise, so never feed possibly damaged outputs into a control.
    write(root/"consumer.cpp","int main(){return pch_value()==11?0:1;}\n");
    const auto layout=ProjectArtifactLayout::create(root);
    require(layout.has_value(),"consumer layout");
    const auto source=layout->for_source(root/"consumer.cpp"); const auto output=layout->for_target("pch-consumer");
    require(source && output,"consumer artifact projection");
    msvc::MsvcLinker linker{*toolchain,runner};
    MsvcIncrementalLinkCoordinator linking{*toolchain,linker};
    MsvcIncrementalTargetCoordinator consumer{compiling,linking};
    IncrementalTargetRequest use;
    use.sources={{root/"consumer.cpp",*source}}; use.target=*output;
    use.compiler_options=changed->record.compiler_options;
    use.compiler_options.precompiled_header->role=PrecompiledHeaderRole::use;
    use.additional_objects={request.artifacts.object};
    use.link_options.configuration=BuildConfiguration::release;
    use.link_options.additional_arguments={"/INCREMENTAL:NO"};
    use.working_directory=root; use.max_parallel_compiles=1;
    runner.phase="consumer"; write(evidence/"consumer.attempt.txt","one ordinary PCH consumer build before expected failure\n");
    const auto built=consumer.run(use);
    if (!built) write(evidence/"consumer.error.txt",built.error().message);
    require(built.has_value(),"consumer compiles with recorded PCH and links paired object");
    runner.phase="program";
    process::ProcessSpec program;
    program.executable=use.target.executable; program.working_directory=root;
    program.capture_stdout=program.capture_stderr=true;
    const auto ran=runner.run(program);
    require(ran && ran->exit_code==0,"PCH consumer program correct");
    const auto before_failure_stamp=fs::last_write_time(request.header);
    std::this_thread::sleep_for(std::chrono::milliseconds{40});
    write(request.header,"#error MQB_PCH_RECORD_EXPECTED_FAILURE\n");
    require(fs::last_write_time(request.header) != before_failure_stamp, "failure input change observed");
    const auto failed=invoke("06-compile-failed");
    require(!failed && failed.error().code==IncrementalPchErrorCode::compile_failed && failed.error().compile_error &&
        !fs::exists(evidence/"06-compile-failed.record.txt"),"failed PCH publishes no success record");
    check_pch_cache_history(evidence/"01-cold", *cold);
    check_pch_cache_history(evidence/"02-reuse", *reuse);
    require(cold->cache_evidence.request.options.configuration == BuildConfiguration::debug &&
        release->cache_evidence.request.options.configuration == BuildConfiguration::release,
        "recorded effective compiler options survive configuration change and later failure");
    require(calls==6,"six native PCH calls completed");
    write(evidence/"completed.txt","6 PCH API calls; 5 success, 1 expected compile failure; 1 consumer build/run before failure; no retries\n");
}
#endif
} // namespace
int main() {
    try {
        model_contracts();
#ifdef _WIN32
        const auto work=fs::current_path();
        require(!fs::exists(work/"storage-fixtures") && !fs::exists(work/"storage-evidence"),"fresh PCH record fixture/evidence required");
        deterministic_cases(work/"storage-fixtures/pch-mock",work/"storage-evidence/pch-records/mock");
        native_cases(work/"storage-fixtures/pch-native",work/"storage-evidence/pch-records/native");
        std::cout << "PCH producer native and mock evidence retained separately\n";
#else
        std::cout << "portable PCH record model only; Windows/PCH coordination/MSVC NOT executed\n";
#endif
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
// BEGIN MQB_PCH_CACHE_EVIDENCE_CASES
#ifdef _WIN32
#include "mqb/core/CompileCacheFile.hpp"
#include "mqb/core/PerformanceEvidence.hpp"
namespace {
void save_pch_cache_evidence(const fs::path& prefix, const RecordedPchResult& result) {
    const auto& e = result.cache_evidence;
    const auto& r = result.record;
    static_assert(!CompileCacheEvidence::producer_identity_verified);
    static_assert(!CompileCacheEvidence::current_content_verified);
    static_assert(!CompileCacheEvidence::complete_producer_inventory);
    static_assert(!CompileCacheEvidence::deletion_authorized);
    static_assert(!PchArtifactRecord::exact_cache_entry_captured);
    require(e.request.unit.source == r.creator.source && e.cache_entry.source == r.creator.source &&
        e.request.cache_file == r.compile_cache && e.request.source_dependencies_file == r.dependencies &&
        e.request.working_directory == r.working_directory,
        "cache evidence is attached to the effective same-call PCH creator, not caller's use binding");
    require(e.request.options.configuration == r.compiler_options.configuration &&
        e.request.options.precompiled_header && e.request.options.precompiled_header->role == PrecompiledHeaderRole::create &&
        e.request.options.precompiled_header->header == r.input_header &&
        e.request.options.precompiled_header->artifact == r.compiler_options.precompiled_header->artifact,
        "effective PCH cache request retains actual options and binding");
    auto paired = [&](const auto& outputs) {
        require(outputs.size() == r.creator.outputs.size() && outputs.size() == 2, "exact paired PCH outputs");
        for (std::size_t i=0; i<outputs.size(); ++i)
            require(outputs[i].path == r.creator.outputs[i].path && outputs[i].kind == r.creator.outputs[i].kind,
                "PCH output order, path and kind preserved");
    };
    paired(e.request.unit.outputs); paired(e.cache_entry.outputs);
    const auto expected = r.cache_state == ArtifactCacheState::saved ? CompileCacheEvidenceState::saved :
        r.cache_state == ArtifactCacheState::reused ? CompileCacheEvidenceState::reused : CompileCacheEvidenceState::save_failed;
    require(e.state == expected, "PCH projection agrees with typed same-call cache state");
    require((e.state == CompileCacheEvidenceState::reused) == !result.result.compile.compiled,
        "reused evidence is not invented production");
    if (e.state == CompileCacheEvidenceState::save_failed) {
        require(e.save_error && e.save_error->code == CompileCacheFileErrorCode::replace_failed &&
            e.save_error->file == r.compile_cache && e.save_error->offset == 0 &&
            e.save_error->message == "failed to remove previous cache entry",
            "PCH preserves original typed save failure, path and message");
        require(bytes(r.compile_cache/"sentinel") == "retain obstruction", "save failure keeps blocking bytes");
    } else require(!e.save_error, "success or reuse does not invent save error");
    if (r.creator_source_materialization_required &&
        std::find(result.result.compile.validation.reasons.begin(), result.result.compile.validation.reasons.end(),
                  BuildReason::source_changed) != result.result.compile.validation.reasons.end())
        require(e.request.force_rebuild, "same-timestamp repair records the final effective forced request");
    const fs::path snapshot = prefix.string()+".captured.cache";
    require(CompileCacheFile::save(snapshot, e.cache_entry).has_value(), "test-only copy with the existing cache codec");
    if (e.state != CompileCacheEvidenceState::save_failed)
        require(bytes(snapshot) == bytes(r.compile_cache), "captured value equals saved/accepted cache bytes");
    std::ostringstream info;
    info << "attached_exact_cache_entry_captured=true\nlegacy_projection_exact_cache_entry_captured=false\n"
        << "state=" << static_cast<int>(e.state) << "\nforce_rebuild=" << e.request.force_rebuild
        << "\nconfiguration=" << static_cast<int>(e.request.options.configuration)
        << "\nsave_error=" << e.save_error.has_value()
        << "\nproducer_identity_verified=false\ncurrent_content_verified=false\n"
        << "complete_producer_inventory=false\ndeletion_authorized=false\n";
    if (e.save_error) info << "save_code=" << static_cast<int>(e.save_error->code)
        << "\nsave_path=" << text(e.save_error->file) << "\nsave_offset=" << e.save_error->offset
        << "\nsave_message=" << e.save_error->message << '\n';
    // Environment values are deliberately neither logged nor serialized.
    write(prefix.string()+".cache-evidence.txt", info.str());
}
void check_pch_cache_history(const fs::path& prefix, const RecordedPchResult& old) {
    const fs::path after = prefix.string()+".history.cache";
    require(CompileCacheFile::save(after, old.cache_evidence.cache_entry).has_value(), "serialize retained old value after failures");
    require(bytes(after) == bytes(prefix.string()+".captured.cache"), "later overwrite/failure cannot mutate earlier captured cache value");
}
std::expected<RecordedPchResult, IncrementalPchError> run_pch_cache_checked(
    MsvcIncrementalPchCoordinator& pch, const IncrementalPchRequest& request,
    ArtifactGenerationLabel label, const fs::path& prefix) {
    performance::Collector collector;
    const auto result = [&]() {
        performance::Activation activation{collector};
        return pch.run_recorded(request, std::move(label));
    }();
    const auto count = collector.snapshot();
    constexpr auto ci = static_cast<std::size_t>(performance::CacheKind::compile);
    const auto name = prefix.filename().string();
    // Counters count successful payload opens, not failed load attempts.
    // Source contracts separately pin the inspect/recheck call sites.
    std::uint64_t expected_reads = 2;
    if (name == "02-reuse" || name == "03-creator-repair" || name == "08-blocked-creator") expected_reads = 1;
    if (name == "09-no-header" || name == "10-invalid") expected_reads = 0;
    require(count.cache_files_opened[ci] <= expected_reads, "recording adds no cache open beyond original PCH path");
    if (name == "02-reuse") require(count.cache_files_opened[ci] == 1, "warm PCH accepts exactly one cache read");
    if (name == "02-reuse") require(count.cache_files_written[ci] == 0, "warm recording does not save again");
    std::ostringstream summary;
    summary << "compile_cache_reads=" << count.cache_files_opened[ci]
        << "\ncompile_cache_writes=" << count.cache_files_written[ci]
        << "\nmaximum_original_payload_opens=" << expected_reads << '\n';
    write(prefix.string()+".cache-counts.txt", summary.str());
    return result;
}
} // namespace
#endif
// END MQB_PCH_CACHE_EVIDENCE_CASES
