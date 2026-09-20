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
#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/orchestration/MsvcIncrementalTargetCoordinator.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"
#endif

namespace {
namespace fs = std::filesystem;
void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string{message});
}
void model_contracts() {
    using namespace mqb;
    static_assert(!LinkArtifactRecord::deletion_authorized);
    static_assert(!LinkArtifactRecord::physical_identity_verified);
    static_assert(!TargetArtifactRecord::deletion_authorized);
    static_assert(!TargetArtifactRecord::complete_producer_inventory);
    LinkCacheEntry entry{.signature=BuildSignature::from_digest({1,2}),
        .objects={"shared.obj"}, .output="app.exe"};
    TargetArtifactRecord first{
        .caller_label=ArtifactGenerationLabel{"app","one"}, .compiler_options={},
        .sources={{.source="main.cpp",.object="shared.obj",.completion=ArtifactCompletion::executed}},
        .additional_object_inputs={"upstream.obj"},
        .link={.completion=ArtifactCompletion::executed, .cache_state=ArtifactCacheState::saved,
               .association=entry, .options={}, .cache_file="app.linkcache"}};
    auto later=first;
    later.caller_label->generation="two";
    later.link.completion=ArtifactCompletion::reused;
    later.compiler_options.configuration=BuildConfiguration::release;
    later.sources[0].object="new.obj";
    require(first.caller_label->generation=="one" && first.sources[0].object=="shared.obj" &&
        first.compiler_options.configuration==BuildConfiguration::debug, "record owns its labels/options/paths");
    require(later.link.association.signature==first.link.association.signature,
        "caller annotations are not build signatures");
    require(first.additional_object_inputs.size()==1 && first.sources.size()==1,
        "upstream inputs do not become ordinary source outputs");
}
#ifdef _WIN32
std::string text(const fs::path& path) {
    const auto bytes = path.generic_u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
std::string path_text(const fs::path& path) { return text(path); }
void write(const fs::path& path, std::string_view content) {
    fs::create_directories(path.parent_path());
    std::ofstream out{path, std::ios::binary};
    out << content;
    out.close();
    require(static_cast<bool>(out), "write record fixture/evidence");
}
struct FileState {
    fs::file_time_type modified;
    std::string contents;
    bool operator==(const FileState&) const = default;
};
std::map<std::string, FileState> snapshot(const fs::path& root) {
    std::map<std::string, FileState> result;
    if (!fs::exists(root)) return result;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        std::ifstream file{entry.path(), std::ios::binary};
        std::string bytes{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
        require(!file.bad(), "read record fixture snapshot");
        result.emplace(text(entry.path().lexically_relative(root)),
                       FileState{entry.last_write_time(), std::move(bytes)});
    }
    return result;
}
class LinkerLikeRunner final : public mqb::process::ProcessRunner {
public:
    fs::path output;
    int calls{};
    bool fail_link{false};
    mqb::process::ProcessSpec last_spec;

    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run(const mqb::process::ProcessSpec& spec) override {
        ++calls;
        last_spec = spec;
        if (fail_link) {
            return mqb::process::ProcessResult{
                .exit_code = 1120,
                .stderr_text = "simulated link.exe diagnostic",
            };
        }
        write(output, "fake executable produced by linker runner");
        return mqb::process::ProcessResult{
            .exit_code = 0,
            .stdout_text = "simulated link.exe success",
        };
    }
};

[[nodiscard]] bool has_warning(
    const mqb::orchestration::IncrementalLinkResult& result,
    const mqb::orchestration::IncrementalLinkWarningCode code) {
    return std::any_of(
        result.warnings.begin(),
        result.warnings.end(),
        [code](const mqb::orchestration::IncrementalLinkWarning& warning) {
            return warning.code == code;
        });
}


void completion_record_cases(const fs::path& root) {
    using namespace mqb;
    using namespace mqb::orchestration;
    fs::create_directories(root);
    const auto cl = root / "tools/cl.exe", link = root / "tools/link.exe";
    write(cl, "compiler identity"); write(link, "linker identity");
    const auto object = root / "obj/source.obj";
    write(object, "input object");
    msvc::MsvcToolchain toolchain{
        .identity = {.compiler=cl, .version="record-test", .binary_stamp="test"},
        .linker=link, .vc_tools_root=root / "tools",
    };
    LinkerLikeRunner runner;
    runner.output = root / "bin/target.exe";
    msvc::MsvcLinker linker{toolchain, runner};
    MsvcIncrementalLinkCoordinator coordinator{toolchain, linker};
    IncrementalLinkRequest request{
        .objects={object}, .output=runner.output, .options={},
        .cache_file=root / "cache/target.linkcache", .working_directory=root,
    };
    const auto bytes = [](const fs::path& file) {
        std::ifstream in{file, std::ios::binary};
        return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    };
    const auto cold = coordinator.run_recorded(request);
    require(cold.has_value(), "recorded cold link succeeds");
    if (!cold) return;
    const auto& first = cold->record;
    require(first.completion == ArtifactCompletion::executed &&
           first.cache_state == ArtifactCacheState::saved, "executed LINK and saved cache are distinct evidence");
    require(first.association.objects == request.objects && first.association.output == request.output,
           "record retains actual terminal object/output associations");
    require(first.cache_file == request.cache_file && first.working_directory == request.working_directory,
           "record carries exact explicit cache/cwd context");
    auto saved = LinkCacheFile::load(request.cache_file);
    require(saved && *saved && (**saved).signature == first.association.signature,
           "executed record is the entry sealed into the real cache");
    require(first.association.side_outputs.empty(), "absent conditional PDB is not fabricated from extension");
    require(!first.deletion_authorized && !first.physical_identity_verified,
           "completion is not physical ownership or deletion permission");
    const auto cache_bytes = bytes(request.cache_file);
    const auto cache_time = fs::last_write_time(request.cache_file);
    const auto warm = coordinator.run_recorded(request);
    require(warm && warm->record.completion == ArtifactCompletion::reused &&
           warm->record.cache_state == ArtifactCacheState::reused && !warm->result.linked,
           "cache hit records reuse rather than a new produced generation");
    if (warm) require(warm->record.association.signature == first.association.signature,
                     "reused entry belongs to the same validated invocation signature");
    require(runner.calls == 1 && bytes(request.cache_file) == cache_bytes &&
           fs::last_write_time(request.cache_file) == cache_time,
           "recorded cache hit adds no linker invocation or cache rewrite");
    request.options.configuration = BuildConfiguration::release;
    const auto release = coordinator.run_recorded(request);
    require(release && release->record.options.configuration == BuildConfiguration::release &&
           release->record.association.signature != first.association.signature,
           "same output pathname records a different configuration/signature");
    require(first.options.configuration == BuildConfiguration::debug,
           "old record is owned data, not a view into mutated request options");

    // A returned success can carry a cache-save warning. Never reload the old
    // disk entry and mislabel it as the completed pass's association.
    auto unsaved = request;
    unsaved.force_relink = true;
    unsaved.cache_file = root / "cache/cannot-replace-directory";
    fs::create_directory(unsaved.cache_file);
    write(unsaved.cache_file / "sentinel", "keep");
    const auto partial = coordinator.run_recorded(unsaved);
    require(partial && partial->record.cache_state == ArtifactCacheState::save_failed &&
           partial->record.completion == ArtifactCompletion::executed,
           "successful link with failed cache save is not claimed durable");
    if (partial) require(has_warning(partial->result, IncrementalLinkWarningCode::cache_save_failed) &&
                        partial->record.association.output == unsaved.output,
                        "original save warning and current in-memory output association retained");
    require(bytes(unsaved.cache_file / "sentinel") == "keep", "recording never deletes unknown cache-path contents");

    auto dll_request = request;
    dll_request.options.target_kind = TargetKind::dynamic_library;
    dll_request.output = root / "bin/component.dll";
    dll_request.cache_file = root / "cache/component.linkcache";
    runner.output = dll_request.output;
    const auto dll = coordinator.run_recorded(dll_request);
    require(dll && dll->record.options.target_kind == TargetKind::dynamic_library &&
           dll->record.association.side_outputs.empty(),
           "DLL identity retained without inventing unobserved import/export files");
    runner.output = request.output;

    // Corrupt old state is reported, then replaced by actual successful work;
    // it cannot become the returned record merely because a file existed.
    write(request.cache_file, "corrupt legacy cache");
    const auto repaired = coordinator.run_recorded(request);
    require(repaired && repaired->record.completion == ArtifactCompletion::executed &&
           has_warning(repaired->result, IncrementalLinkWarningCode::cache_load_failed),
           "corrupt legacy cache is not promoted to successful reuse");
    const auto before_failure = bytes(request.cache_file);
    request.force_relink = true;
    runner.fail_link = true;
    const auto failed = coordinator.run_recorded(request);
    require(!failed && failed.error().code == IncrementalLinkErrorCode::link_failed &&
           failed.error().linker_error.has_value(), "failed link returns original error, not a completion record");
    require(bytes(request.cache_file) == before_failure, "failed recorded link does not publish new cache state");
    runner.fail_link = false;
    request.options.additional_arguments.push_back("/MAP:" + path_text(root / "missing.map"));
    const auto missing_map = coordinator.run_recorded(request);
    require(!missing_map && missing_map.error().code == IncrementalLinkErrorCode::link_failed,
           "tool exit zero without required map output cannot publish a completion record");
    require(bytes(request.cache_file) == before_failure, "missing required output preserves previous cache");
}

// Real MSVC integration of the opt-in completion API. This fixture writes its
// audit outside the build tree; the product API itself does not persist records.
void recorded_target_lifecycle(const fs::path& root, const fs::path& evidence) {
    using namespace mqb;
    using namespace mqb::orchestration;
    fs::create_directories(root);
    fs::create_directories(evidence);
    struct RetainingRunner final : process::ProcessRunner {
        platform::windows::WindowsProcessRunner native;
        fs::path evidence;
        unsigned sequence{};
        std::string phase{"discovery"};
        explicit RetainingRunner(fs::path destination) : evidence(std::move(destination)) {}
        std::expected<process::ProcessResult, process::ProcessError>
        run(const process::ProcessSpec& spec) override {
            const auto prefix = evidence / (std::to_string(++sequence) + "-" + phase);
            std::ostringstream command;
            command << "executable=" << text(spec.executable) << '\n';
            if (spec.working_directory) command << "cwd=" << text(*spec.working_directory) << '\n';
            for (const auto& arg : spec.arguments) command << "arg=" << arg << '\n';
            write(prefix.string() + ".argv.txt", command.str());
            auto result = native.run(spec);
            if (!result) write(prefix.string() + ".launch-error.txt",
                std::to_string(result.error().native_code) + " " + result.error().message);
            else {
                write(prefix.string() + ".stdout.txt", result->stdout_text);
                write(prefix.string() + ".stderr.txt", result->stderr_text);
                write(prefix.string() + ".exit.txt", std::to_string(result->exit_code));
            }
            return result;
        }
    } runner{evidence / "processes"};
    // Fixed single-worker observation: the recorder itself is intentionally
    // serial. This is not a multiwriter/lease or concurrent-target experiment.
    msvc::MsvcToolchainLocator locator{runner};
    msvc::DiscoveryOptions discovery;
    discovery.preference = msvc::ToolchainPreference::visual_studio;
    discovery.cache_file = root / ".mqb/cache/toolchain/vs-x64.cache";
    const auto toolchain = locator.discover(discovery);
    if (!toolchain) write(evidence / "discovery-error.txt", toolchain.error().message);
    require(toolchain.has_value(), "recorded target real toolchain discovery");
    msvc::MsvcCompileExecutor executor{*toolchain, runner};
    MsvcIncrementalCompileCoordinator compiling{*toolchain, executor};
    msvc::MsvcLinker linker{*toolchain, runner};
    MsvcIncrementalLinkCoordinator linking{*toolchain, linker};
    MsvcIncrementalTargetCoordinator target{compiling, linking};
    const auto layout = ProjectArtifactLayout::create(root);
    require(layout.has_value(), "record fixture artifact layout");
    write(root / "main.cpp", "int helper(); int main() { return helper() == 7 ? 0 : 1; }\n");
    write(root / "helper.cpp", "int helper() { return 7; }\n");
    IncrementalTargetRequest request;
    request.working_directory = root;
    request.max_parallel_compiles = 1;
    request.link_options.additional_arguments = {"/INCREMENTAL:NO"};
    for (const auto* filename : {"main.cpp", "helper.cpp"}) {
        const auto artifacts = layout->for_source(root / filename);
        require(artifacts.has_value(), "record fixture source layout");
        request.sources.push_back({root / filename, *artifacts});
    }
    auto set_target = [&](const char* name, TargetKind kind = TargetKind::executable) {
        const auto artifacts = layout->for_target(name, kind);
        require(artifacts.has_value(), "record fixture target layout");
        request.target = *artifacts;
        request.link_options.target_kind = kind;
    };
    set_target("recorded");
    unsigned completed_calls = 0;
    auto run = [&](const char* phase) {
        runner.phase = phase;
        write(evidence / (std::string{phase} + ".attempt.txt"), "recorded API invocation\n");
        auto result = target.run_recorded(request, ArtifactGenerationLabel{"fixture", phase});
        ++completed_calls;
        if (!result) write(evidence / (std::string{phase} + ".error.txt"), result.error().message);
        else {
            std::ostringstream out;
            const auto& record = result->record;
            out << "caller_generation=" << record.caller_label->generation << '\n'
                << "compile_configuration=" << to_string(record.compiler_options.configuration) << '\n'
                << "link_configuration=" << to_string(record.link.options.configuration) << '\n'
                << "target_kind=" << to_string(record.link.options.target_kind) << '\n'
                << "linked=" << result->result.link.linked << '\n'
                << "signature=" << record.link.association.signature.hex() << '\n'
                << "output=" << text(record.link.association.output) << '\n'
                << "deletion_authorized=false\nphysical_identity_verified=false\n";
            for (const auto& source : record.sources) {
                out << "source=" << text(source.source) << '\n'
                    << "object=" << text(source.object) << '\n'
                    << "compiled=" << (source.completion == ArtifactCompletion::executed) << '\n';
                require(fs::is_regular_file(source.object), "successful native record references an existing object");
            }
            for (const auto& file : record.link.association.side_outputs) {
                out << "side_output=" << text(file) << '\n';
                require(fs::is_regular_file(file), "native tracked side output actually exists");
            }
            write(evidence / (std::string{phase} + ".record.txt"), out.str());
            require(fs::is_regular_file(record.link.association.output), "native terminal output exists");
        }
        return result;
    };
    const auto cold = run("01-cold");
    require(cold && cold->record.link.completion == ArtifactCompletion::executed, "native record cold execution");
    const auto before = snapshot(root / ".mqb");
    const auto calls_before = runner.sequence;
    const auto warm = run("02-reuse");
    require(warm && warm->record.link.completion == ArtifactCompletion::reused && runner.sequence == calls_before,
        "native recorded reuse launches no tools");
    require(before == snapshot(root / ".mqb"), "native recorded reuse does not rewrite cache or outputs");
    set_target("shared");
    const auto shared = run("03-shared");
    require(shared && !shared->result.any_compiled && shared->record.sources[0].object == cold->record.sources[0].object,
        "native shared target retains common object references");
    set_target("recorded");
    request.compiler_options.configuration = BuildConfiguration::release;
    request.link_options.configuration = BuildConfiguration::release;
    const auto release = run("04-release");
    require(release && release->record.link.association.signature != cold->record.link.association.signature &&
        cold->record.compiler_options.configuration == BuildConfiguration::debug,
        "native configuration overwrite preserves historical value record");
    fs::rename(root / "helper.cpp", root / "renamed.cpp");
    const auto renamed_artifacts = layout->for_source(root / "renamed.cpp");
    require(renamed_artifacts.has_value(), "renamed source layout");
    request.sources[1] = {root / "renamed.cpp", *renamed_artifacts};
    const auto renamed = run("05-renamed");
    require(renamed && fs::exists(cold->record.sources[1].object), "native rename preserves old object");
    runner.phase = "program";
    process::ProcessSpec program;
    program.executable = request.target.executable;
    program.working_directory = root;
    program.capture_stdout = program.capture_stderr = true;
    const auto executed = runner.run(program);
    require(executed && executed->exit_code == 0, "recorded native executable remains correct");
    const auto link_cache_before_failure = snapshot(root / ".mqb/cache/link");
    write(root / "renamed.cpp", "static_assert(false, \"MQB_RECORD_EXPECTED_FAILURE\"); int helper(){return 7;}\n");
    request.force_downstream_rebuild = true;
    const auto failed = run("06-failed");
    require(!failed && failed.error().code == IncrementalTargetErrorCode::compile_failed,
        "real failed compile has no successful target record");
    require(!fs::exists(evidence / "06-failed.record.txt") &&
        link_cache_before_failure == snapshot(root / ".mqb/cache/link"),
        "failed record cannot replace successful link cache/generation");
    // Separate DLL target, not a retry of the deliberately failed EXE build.
    write(root / "export.cpp", "extern \"C\" __declspec(dllexport) int value(){return 7;}\n");
    const auto export_artifacts = layout->for_source(root / "export.cpp");
    require(export_artifacts.has_value(), "DLL source layout");
    request.sources = {{root / "export.cpp", *export_artifacts}};
    request.force_downstream_rebuild = false;
    set_target("component", TargetKind::dynamic_library);
    const auto dll = run("07-dll");
    require(dll && dll->record.link.options.target_kind == TargetKind::dynamic_library &&
        !dll->record.link.association.side_outputs.empty(), "real DLL tracks observed import/export side outputs");
    require(completed_calls == 7, "fixed seven recorded API calls completed");
    write(evidence / "completed.txt", "seven API calls: six successes, one expected compile failure; no retries\n");
}

#endif
} // namespace

int main() {
    try {
        model_contracts();
#ifdef _WIN32
        const auto work = fs::current_path();
        const auto root = work / "storage-fixtures";
        require(!fs::exists(root), "fresh artifact-record fixture required; do not overwrite evidence");
        completion_record_cases(root / "deterministic");
        recorded_target_lifecycle(root / "recorded-target", work / "storage-evidence/records");
        std::cout << "successful invocation record native evidence retained\n";
#else
        std::cout << "portable record ownership model only; native coordinators/MSVC NOT executed\n";
#endif
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
