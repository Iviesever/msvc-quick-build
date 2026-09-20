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
    msvc::MsvcCompileExecutor executor{toolchain, runner};
    MsvcIncrementalCompileCoordinator compiling{toolchain, executor};
    MsvcIncrementalPchCoordinator pch{compiling};
    unsigned calls{};
    auto invoke = [&](const char* phase, const IncrementalPchRequest& input) {
        require(++calls <= 10, "mock PCH API budget"); runner.phase = phase;
        write(evidence/(std::string{phase}+".attempt.txt"), "mock cl; real PCH/compile/cache pipeline\n");
        auto r = pch.run_recorded(input, ArtifactGenerationLabel{"mock-pch",phase});
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
        auto r=pch.run_recorded(request,ArtifactGenerationLabel{"native-pch",phase});
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
