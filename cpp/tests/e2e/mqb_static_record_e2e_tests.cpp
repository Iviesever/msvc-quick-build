#include <algorithm>
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
#include <utility>
#include <vector>

#include "mqb/core/BuildArtifactRecord.hpp"
#ifdef _WIN32
#include "mqb/core/ArchiveCacheFile.hpp"
#include "mqb/orchestration/MsvcIncrementalStaticTargetCoordinator.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"
#endif

namespace {
namespace fs = std::filesystem;
using namespace mqb;
void require(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string{message});
}
void model_contracts() {
    static_assert(!ArchiveArtifactRecord::deletion_authorized);
    static_assert(!ArchiveArtifactRecord::physical_identity_verified);
    static_assert(!StaticTargetArtifactRecord::deletion_authorized);
    static_assert(!StaticTargetArtifactRecord::complete_producer_inventory);
    StaticTargetArtifactRecord first{
        .caller_label=ArtifactGenerationLabel{"library", "first"}, .compiler_options={},
        .sources={{.source="source.cpp", .object="source.obj", .completion=ArtifactCompletion::executed}},
        .additional_object_inputs={"upstream.obj"},
        .archive={.completion=ArtifactCompletion::executed, .cache_state=ArtifactCacheState::saved,
            .association={.signature=BuildSignature::from_digest({1,2}),
                .objects={"upstream.obj", "source.obj"}, .output="target.lib"},
            .architecture=Architecture::x64, .link_time_code_generation=false,
            .additional_arguments={"/WX"}, .cache_file="target.archivecache", .working_directory="project"}};
    auto later = first;
    later.caller_label->generation = "second";
    later.compiler_options.configuration = BuildConfiguration::release;
    later.sources.front().object = "renamed.obj";
    later.archive.additional_arguments.clear();
    require(first.caller_label->generation == "first" && first.sources.front().object == "source.obj" &&
        first.archive.additional_arguments.size() == 1, "static record owns labels, paths and routed arguments");
    require(first.archive.association.signature == later.archive.association.signature &&
        first.compiler_options.configuration != later.compiler_options.configuration,
        "archive recipe alone does not identify compile configuration/content generation");
    require(first.sources.size() == 1 && first.additional_object_inputs.size() == 1,
        "upstream inputs do not become source-produced outputs");
}
#ifdef _WIN32
using namespace mqb::orchestration;
std::string text(const fs::path& path) {
    const auto bytes = path.generic_u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
void write(const fs::path& path, std::string_view data) {
    fs::create_directories(path.parent_path());
    std::ofstream out{path, std::ios::binary};
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.close();
    require(static_cast<bool>(out), "write static fixture/evidence");
}
std::string bytes(const fs::path& path) {
    std::ifstream file{path, std::ios::binary};
    require(file.is_open(), "open static fixture snapshot");
    std::string data{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    require(!file.bad(), "read static fixture snapshot");
    return data;
}
struct FileState {
    fs::file_time_type modified;
    std::string contents;
    bool operator==(const FileState&) const = default;
};
std::map<std::string, FileState> snapshot(const fs::path& root) {
    std::map<std::string, FileState> result;
    if (fs::exists(root)) for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) result.emplace(text(entry.path().lexically_relative(root)),
            FileState{entry.last_write_time(), bytes(entry.path())});
    }
    return result;
}
std::string describe(const ArchiveArtifactRecord& record) {
    std::ostringstream out;
    out << "completion=" << (record.completion == ArtifactCompletion::executed ? "executed" : "reused") << '\n';
    const char* state = record.cache_state == ArtifactCacheState::saved ? "saved" :
        record.cache_state == ArtifactCacheState::reused ? "reused" : "save_failed";
    out << "cache_state=" << state << "\narchitecture=" << to_string(record.architecture)
        << "\nltcg=" << record.link_time_code_generation
        << "\nsignature=" << record.association.signature.hex()
        << "\noutput=" << text(record.association.output)
        << "\ncache_file=" << text(record.cache_file)
        << "\ncwd=" << text(record.working_directory)
        << "\ndelete_authorized=false\nphysical_identity_verified=false\n";
    for (const auto& object : record.association.objects) out << "archive_input=" << text(object) << '\n';
    for (const auto& arg : record.additional_arguments) out << "routed_argument=" << arg << '\n';
    return out.str();
}
void log_process(const fs::path& prefix, const process::ProcessSpec& spec) {
    std::ostringstream out;
    out << "executable=" << text(spec.executable) << '\n';
    if (spec.working_directory) out << "cwd=" << text(*spec.working_directory) << '\n';
    for (const auto& arg : spec.arguments) out << "arg=" << arg << '\n';
    write(prefix.string() + ".argv.txt", out.str());
}
void log_result(const fs::path& prefix, const std::expected<process::ProcessResult, process::ProcessError>& result) {
    if (!result) write(prefix.string() + ".launch-error.txt", result.error().message);
    else {
        write(prefix.string() + ".exit.txt", std::to_string(result->exit_code));
        write(prefix.string() + ".stdout.txt", result->stdout_text);
        write(prefix.string() + ".stderr.txt", result->stderr_text);
    }
}

void deterministic_cases(const fs::path& root, const fs::path& evidence) {
    struct MockRunner final : process::ProcessRunner {
        fs::path evidence;
        std::string phase;
        unsigned calls{};
        bool fail{false}, omit_output{false};
        explicit MockRunner(fs::path p) : evidence(std::move(p)) {}
        std::expected<process::ProcessResult, process::ProcessError> run(const process::ProcessSpec& spec) override {
            const auto prefix = evidence / (std::to_string(++calls) + "-" + phase);
            log_process(prefix, spec);
            const auto arg = std::find_if(spec.arguments.begin(), spec.arguments.end(),
                [](const auto& value) { return value.starts_with("/OUT:"); });
            require(arg != spec.arguments.end(), "mock receives librarian-owned output argument");
            if (!fail && !omit_output) {
                const auto value = arg->substr(5);
                write(fs::path{std::u8string{reinterpret_cast<const char8_t*>(value.data()), value.size()}}, "mock archive\n");
            }
            std::expected<process::ProcessResult, process::ProcessError> result = process::ProcessResult{
                .exit_code=fail ? 1136 : 0, .stdout_text="mock LIB result", .stderr_text=fail ? "MOCK_ARCHIVE_FAILURE" : ""};
            log_result(prefix, result);
            return result;
        }
    } runner{evidence / "processes"};
    write(root / "tools/lib.exe", "mock librarian identity");
    write(root / "input.obj", "mock object input");
    msvc::MsvcToolchain toolchain{
        .identity={.compiler=root / "tools/cl.exe", .version="static-record-mock", .binary_stamp="test"},
        .librarian=root / "tools/lib.exe", .vc_tools_root=root / "tools"};
    msvc::MsvcLibrarian librarian{toolchain, runner};
    MsvcIncrementalArchiveCoordinator archive{toolchain, librarian};
    IncrementalArchiveRequest request{
        .objects={root / "input.obj"}, .output=root / "bin/target.lib",
        .cache_file=root / "cache/target.archivecache", .working_directory=root,
        .architecture=Architecture::x64, .additional_arguments={"/WX"}};
    unsigned api_calls{};
    auto invoke = [&](const char* phase, const IncrementalArchiveRequest& input) {
        require(++api_calls <= 10, "deterministic archive API budget");
        runner.phase = phase;
        write(evidence / (std::string{phase} + ".attempt.txt"), "mock process; real archive coordinator and cache I/O\n");
        auto result = archive.run_recorded(input);
        if (result) write(evidence / (std::string{phase} + ".record.txt"), describe(result->record));
        else {
            std::string error = "code=" + std::to_string(static_cast<int>(result.error().code)) +
                "\nmessage=" + result.error().message;
            if (result.error().librarian_error) error += "\nlib_message=" + result.error().librarian_error->message;
            write(evidence / (std::string{phase} + ".error.txt"), error);
        }
        return result;
    };
    const auto cold = invoke("01-cold", request);
    require(cold && cold->record.completion == ArtifactCompletion::executed &&
        cold->record.cache_state == ArtifactCacheState::saved, "archive execution records successful cache save");
    require(cold->record.association.output == request.output && !fs::exists(request.output.string()+".mqb-tmp"),
        "record uses installed output, not librarian temporary path");
    const auto before = snapshot(root);
    const auto warm = invoke("02-reuse", request);
    require(warm && warm->record.completion == ArtifactCompletion::reused &&
        warm->record.cache_state == ArtifactCacheState::reused && runner.calls == 1 && before == snapshot(root),
        "archive reuse starts no LIB and changes no fixture file");
    request.additional_arguments = {"/LTCG", "/WX"};
    const auto ltcg = invoke("03-routed-ltcg", request);
    require(ltcg && ltcg->record.link_time_code_generation && !request.link_time_code_generation &&
        ltcg->record.additional_arguments == std::vector<std::string>{"/WX"} &&
        ltcg->record.association.signature != cold->record.association.signature,
        "record uses effective routing, not raw request flag or duplicate interpreter");
    const auto ltcg_warm = invoke("04-reused-ltcg", request);
    require(ltcg_warm && ltcg_warm->record.link_time_code_generation &&
        ltcg_warm->record.completion == ArtifactCompletion::reused && runner.calls == 2 &&
        ltcg_warm->record.additional_arguments == ltcg->record.additional_arguments &&
        ltcg_warm->record.association.signature == ltcg->record.association.signature,
        "cache-hit record preserves effective routed recipe");
    auto unsaved = request;
    unsaved.force_archive = true;
    unsaved.cache_file = root / "blocked-cache";
    write(unsaved.cache_file / "sentinel", "preserve");
    const auto partial = invoke("05-save-failed", unsaved);
    require(partial && partial->record.cache_state == ArtifactCacheState::save_failed &&
        std::any_of(partial->result.warnings.begin(), partial->result.warnings.end(), [](const auto& warning) {
            return warning.code == IncrementalArchiveWarningCode::cache_save_failed;
        }) && bytes(unsaved.cache_file / "sentinel") == "preserve", "save warning does not become durable success");
    write(request.cache_file, "corrupt archive cache");
    const auto repaired = invoke("06-corrupt", request);
    require(repaired && repaired->record.completion == ArtifactCompletion::executed &&
        std::any_of(repaired->result.warnings.begin(), repaired->result.warnings.end(), [](const auto& warning) {
            return warning.code == IncrementalArchiveWarningCode::cache_load_failed;
        }), "corruption warning retained instead of invented reuse");
    const auto loaded = ArchiveCacheFile::load(request.cache_file);
    require(loaded && *loaded && (**loaded).signature == repaired->record.association.signature,
        "record matches actual saved cache (test-only readback)");
    auto invalid = request;
    invalid.additional_arguments = {"/MACHINE:X86"};
    const auto rejected = invoke("07-parameter-conflict", invalid);
    require(!rejected && rejected.error().code == IncrementalArchiveErrorCode::librarian_parameter_invalid &&
        runner.calls == 4, "parameter rejection publishes no record and launches no LIB");
    const auto good_cache = bytes(request.cache_file), good_output = bytes(request.output);
    request.force_archive = true;
    runner.fail = true;
    const auto failed = invoke("08-tool-failed", request);
    require(!failed && failed.error().librarian_error && failed.error().librarian_error->process_result &&
        failed.error().librarian_error->process_result->exit_code == 1136 &&
        bytes(request.cache_file) == good_cache && bytes(request.output) == good_output,
        "original LIB failure retained without success record or cache replacement");
    runner.fail = false; runner.omit_output = true;
    const auto missing = invoke("09-missing-temporary", request);
    require(!missing && missing.error().librarian_error &&
        missing.error().librarian_error->code == msvc::LibrarianErrorCode::output_install_failed &&
        bytes(request.cache_file) == good_cache && bytes(request.output) == good_output,
        "tool exit zero without installed output cannot publish success");
    runner.omit_output = false;
    auto blocked = request;
    blocked.output = root / "bin/nonempty.lib";
    write(blocked.output / "sentinel", "preserve");
    const auto install_failed = invoke("10-install-failed", blocked);
    require(!install_failed && install_failed.error().librarian_error &&
        install_failed.error().librarian_error->code == msvc::LibrarianErrorCode::output_install_failed &&
        bytes(blocked.output / "sentinel") == "preserve" && bytes(request.cache_file) == good_cache,
        "failed output installation keeps original error and does not authorize cleanup");
    require(api_calls == 10 && runner.calls == 7, "fixed mock API/process budgets completed");
    write(evidence / "completed.txt", "10 archive API calls; 7 mock process calls; no retries\n");
}

void native_static_lifecycle(const fs::path& root, const fs::path& evidence) {
    struct Runner final : process::ProcessRunner {
        platform::windows::WindowsProcessRunner native;
        fs::path evidence;
        std::string phase{"discovery"};
        unsigned calls{}, library_calls{};
        explicit Runner(fs::path p) : evidence(std::move(p)) {}
        std::expected<process::ProcessResult, process::ProcessError> run(const process::ProcessSpec& spec) override {
            const auto prefix = evidence / (std::to_string(++calls) + "-" + phase);
            if (spec.executable.filename() == "lib.exe") ++library_calls;
            log_process(prefix, spec);
            auto result = native.run(spec);
            log_result(prefix, result);
            return result;
        }
    } runner{evidence / "processes"};
    fs::create_directories(root);
    msvc::MsvcToolchainLocator locator{runner};
    msvc::DiscoveryOptions discovery;
    discovery.preference = msvc::ToolchainPreference::visual_studio;
    discovery.cache_file = root / ".mqb/cache/toolchain/vs-x64.cache";
    const auto toolchain = locator.discover(discovery);
    if (!toolchain) write(evidence / "discovery-error.txt", toolchain.error().message);
    require(toolchain.has_value(), "native static record toolchain discovery");
    msvc::MsvcCompileExecutor executor{*toolchain, runner};
    MsvcIncrementalCompileCoordinator compiling{*toolchain, executor};
    msvc::MsvcLibrarian librarian{*toolchain, runner};
    MsvcIncrementalArchiveCoordinator archiving{*toolchain, librarian};
    MsvcIncrementalStaticTargetCoordinator target{compiling, archiving};
    const auto layout = ProjectArtifactLayout::create(root);
    require(layout.has_value(), "native static artifact layout");
    auto source = [&](const char* name) {
        const auto artifacts = layout->for_source(root / name);
        require(artifacts.has_value(), "static source artifacts");
        return TargetSourceRequest{root / name, *artifacts};
    };
    auto artifact = [&](const char* name, TargetKind kind) {
        const auto a = layout->for_target(name, kind);
        require(a.has_value(), "static/consumer target artifacts");
        return *a;
    };
    write(root / "left.cpp", "int left(){return 3;}\n");
    write(root / "right.cpp", "int right(){return 4;}\n");
    IncrementalStaticTargetRequest request;
    request.working_directory = root;
    request.max_parallel_compiles = 1;
    request.sources = {source("left.cpp"), source("right.cpp")};
    request.target = artifact("recorded-static", TargetKind::static_library);
    unsigned api_calls{};
    auto invoke = [&](const char* phase, const IncrementalStaticTargetRequest& input) {
        require(++api_calls <= 6, "static recorded API budget");
        runner.phase = phase;
        write(evidence / (std::string{phase} + ".attempt.txt"), "real static recorded API invocation\n");
        auto result = target.run_recorded(input, ArtifactGenerationLabel{"static-fixture", phase});
        if (!result) write(evidence / (std::string{phase} + ".error.txt"), result.error().message);
        else {
            const auto& record = result->record;
            std::ostringstream out;
            out << describe(record.archive) << "caller_generation=" << record.caller_label->generation
                << "\ncompile_configuration=" << to_string(record.compiler_options.configuration) << '\n';
            for (const auto& s : record.sources) out << "source=" << text(s.source) << "\nobject=" << text(s.object)
                << "\ncompiled=" << (s.completion == ArtifactCompletion::executed) << '\n';
            for (const auto& object : record.additional_object_inputs) out << "upstream_input=" << text(object) << '\n';
            write(evidence / (std::string{phase} + ".record.txt"), out.str());
            // Test evidence only, outside .mqb: exact per-phase archive/cache
            // copies do not make the product API persistent or concurrency-safe.
            write(evidence / (std::string{phase} + ".archivecache"), bytes(record.archive.cache_file));
            write(evidence / (std::string{phase} + ".lib"), bytes(record.archive.association.output));
            require(fs::is_regular_file(record.archive.association.output), "recorded static output exists");
        }
        return result;
    };
    const auto cold = invoke("01-cold", request);
    require(cold && cold->record.archive.completion == ArtifactCompletion::executed &&
        cold->record.sources.size() == 2, "cold static call owns two source associations");
    const auto before = snapshot(root / ".mqb");
    const auto calls_before = runner.calls;
    const auto warm = invoke("02-reuse", request);
    require(warm && warm->record.archive.completion == ArtifactCompletion::reused &&
        runner.calls == calls_before && before == snapshot(root / ".mqb"), "static reuse does not launch tools or rewrite outputs");
    auto shared_request = request;
    shared_request.target = artifact("shared-static", TargetKind::static_library);
    shared_request.additional_objects = {request.sources.front().artifacts.object};
    shared_request.sources.erase(shared_request.sources.begin());
    const auto shared = invoke("03-shared-upstream", shared_request);
    require(shared && !shared->result.any_compiled && shared->record.sources.size() == 1 &&
        shared->record.additional_object_inputs == shared_request.additional_objects &&
        shared->record.archive.association.objects == cold->record.archive.association.objects,
        "upstream/shared object is an archive input, not falsely claimed as produced source output");
    request.compiler_options.configuration = BuildConfiguration::release;
    const auto release = invoke("04-release", request);
    require(release && release->result.any_compiled && release->record.compiler_options.configuration == BuildConfiguration::release &&
        cold->record.compiler_options.configuration == BuildConfiguration::debug &&
        release->record.archive.association.signature == cold->record.archive.association.signature,
        "compiler configuration changes while unchanged LIB recipe can retain its signature");
    fs::rename(root / "right.cpp", root / "renamed.cpp");
    request.sources[1] = source("renamed.cpp");
    const auto renamed = invoke("05-renamed", request);
    require(renamed && renamed->record.sources[0].completion == ArtifactCompletion::reused &&
        renamed->record.sources[1].completion == ArtifactCompletion::executed &&
        renamed->record.archive.association.signature != release->record.archive.association.signature &&
        fs::exists(cold->record.sources[1].object), "source rename is recorded without deleting historical objects");
    const auto archived_before_failure = bytes(request.target.executable);
    const auto cache_before_failure = bytes(request.target.link_cache);
    const auto lib_calls = runner.library_calls;
    write(root / "left.cpp", "static_assert(false, \"MQB_STATIC_RECORD_EXPECTED_FAILURE\"); int left(){return 3;}\n");
    request.force_downstream_rebuild = true;
    const auto failed = invoke("06-compile-failed", request);
    require(!failed && failed.error().code == IncrementalStaticTargetErrorCode::compile_failed &&
        runner.library_calls == lib_calls && !fs::exists(evidence / "06-compile-failed.record.txt") &&
        bytes(request.target.executable) == archived_before_failure && bytes(request.target.link_cache) == cache_before_failure,
        "compile failure publishes no archive record, launches no LIB, and preserves the last library/cache");

    // One independent EXE consumer proves the previously successful library
    // remains linkable. It does NOT retry or repair the failed static target.
    write(root / "consumer.cpp", "int left(); int right(); int main(){return left()+right()==7?0:1;}\n");
    msvc::MsvcLinker linker{*toolchain, runner};
    MsvcIncrementalLinkCoordinator linking{*toolchain, linker};
    MsvcIncrementalTargetCoordinator consumer{compiling, linking};
    IncrementalTargetRequest executable;
    executable.sources = {source("consumer.cpp")};
    executable.target = artifact("static-consumer", TargetKind::executable);
    executable.compiler_options.configuration = BuildConfiguration::release;
    executable.link_options.configuration = BuildConfiguration::release;
    executable.link_options.libraries = {text(request.target.executable)};
    executable.link_options.additional_arguments = {"/INCREMENTAL:NO"};
    executable.working_directory = root;
    executable.max_parallel_compiles = 1;
    runner.phase = "07-consumer";
    write(evidence / "07-consumer.attempt.txt", "one ordinary consumer build, not a static retry\n");
    const auto built = consumer.run(executable);
    if (!built) write(evidence / "07-consumer.error.txt", built.error().message);
    require(built.has_value(), "consumer links the last successfully recorded static library");
    runner.phase = "08-program";
    process::ProcessSpec program;
    program.executable = executable.target.executable;
    program.working_directory = root;
    program.capture_stdout = program.capture_stderr = true;
    const auto ran = runner.run(program);
    require(ran && ran->exit_code == 0, "static library consumer executes correctly");
    require(api_calls == 6, "six real static calls completed");
    write(evidence / "completed.txt", "6 static API calls: 5 successes, 1 expected failure; 1 consumer build/run; no retries\n");
}
#endif
} // namespace

int main() {
    try {
        model_contracts();
#ifdef _WIN32
        const auto work = fs::current_path();
        const auto fixtures = work / "storage-fixtures";
        require(!fs::exists(fixtures) && !fs::exists(work / "storage-evidence"), "fresh static fixture and evidence required");
        deterministic_cases(fixtures / "deterministic-static", work / "storage-evidence/static-records/mock");
        native_static_lifecycle(fixtures / "static-target", work / "storage-evidence/static-records/native");
        std::cout << "static record native and mock evidence retained separately\n";
#else
        std::cout << "portable static record model only; native archive/coordinators/MSVC NOT executed\n";
#endif
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
