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
#include <utility>
#include <vector>

#include "mqb/core/BuildArtifactRecord.hpp"
#ifdef _WIN32
#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/orchestration/MsvcIncrementalTargetCoordinator.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"
#include "TargetWaveCacheEvidenceChecks.hpp"
#include "mqb/orchestration/ArtifactStorageProjection.hpp"
#include "mqb/platform/windows/PathIdentity.hpp"
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

// BEGIN link observation role compatibility controls
// The compiler half below is a pure model, not a claimed compiler invocation.
// The LINK half uses the real coordinator/cache with an in-process fake runner.
void library_observation_role_cases(const fs::path& root, const fs::path& evidence) {
    using namespace mqb;
    using namespace mqb::orchestration;
    fs::create_directories(root);
    fs::create_directories(evidence);
    const auto object = root / "obj/input.obj";
    const auto declared = root / "libs/declared.lib";
    const auto dependency = root / "libs/transitive.lib";
    const auto output = root / "bin/plugin.dll";
    const auto import = msvc::MsvcLinker::import_library_path(output);
    const auto exports = msvc::MsvcLinker::export_file_path(output);
    const auto cl = root / "tools/cl.exe", link = root / "tools/link.exe";
    write(cl, "model compiler"); write(link, "model linker");
    write(object, "model object"); write(declared, "declared library");
    write(dependency, "transitive library");
    msvc::MsvcToolchain toolchain{
        .identity = {.compiler=cl, .version="role-test", .binary_stamp="model"},
        .linker=link, .vc_tools_root=root / "tools",
    };
    struct RoleRunner final : process::ProcessRunner {
        fs::path output, import, exports, dependency;
        unsigned calls{};
        bool fail{}, read_import{};
        std::string stdout_text;
        std::vector<std::string> arguments;
        std::expected<process::ProcessResult, process::ProcessError>
        run(const process::ProcessSpec& spec) override {
            ++calls; arguments = spec.arguments;
            if (fail) return process::ProcessResult{.exit_code=1120,
                .stderr_text="ROLE_EXPECTED_LINK_FAILURE"};
            write(import, "created import library"); write(exports, "created export file");
            write(output, "model DLL");
            // Deterministic freshness ordering without sleeping or hiding probes.
            fs::last_write_time(output, std::max(fs::last_write_time(output),
                fs::last_write_time(dependency)) + std::chrono::seconds{2});
            stdout_text = "  Creating library " + text(import) + " and object " + text(exports)
                + "\r\n    Searching " + text(dependency) + ":\r\n";
            if (read_import) stdout_text += "    Loaded " + text(import) + "(actual-input.obj)\r\n";
            return process::ProcessResult{.exit_code=0, .stdout_text=stdout_text};
        }
    } runner;
    runner.output=output; runner.import=import; runner.exports=exports; runner.dependency=dependency;
    msvc::MsvcLinker linker{toolchain, runner};
    MsvcIncrementalLinkCoordinator linking{toolchain, linker};
    IncrementalLinkRequest request{.objects={object}, .output=output,
        .options={.configuration=BuildConfiguration::release, .target_kind=TargetKind::dynamic_library},
        .cache_file=root / "cache/plugin.linkcache", .working_directory=root};
    request.options.libraries={text(declared)};
    request.options.library_directories={declared.parent_path(), output.parent_path()};
    const auto bytes = [](const fs::path& file) {
        std::ifstream in{file, std::ios::binary};
        return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    };
    const auto contains = [](const auto& paths, const fs::path& path) {
        const auto key=platform::windows::path_identity_key(path);
        return std::any_of(paths.begin(), paths.end(), [&](const auto& item) {
            return platform::windows::path_identity_key(item)==key;
        });
    };
    const auto project = [&](const RecordedLinkResult& value) {
        // Synthetic compile context joins the actual returned LINK value solely
        // to exercise the unchanged rich consumer's whole-result alias guard.
        const auto source=root / "model.cpp", deps=root / "deps/model.json", cache=root / "cache/model.cache";
        RecordedTargetResult model{.record={.link=value.record}};
        model.record.sources.push_back({source, object, deps, cache, ArtifactCompletion::reused, false});
        model.result.compiles.push_back({.source=source});
        model.cache_evidence.compiles.push_back(CompileCacheEvidence{
            .request={.unit={.source=source, .outputs={{object,ArtifactKind::object}}},
                .cache_file=cache, .source_dependencies_file=deps, .working_directory=root},
            .inspection_toolchain=toolchain,
            .cache_entry={.source=source, .toolchain=toolchain.identity,
                .signature=BuildSignature::from_digest({91,92}), .outputs={{object,ArtifactKind::object}}},
            .state=CompileCacheEvidenceState::reused});
        model.record.link=value.record; model.result.link=value.result;
        return project_storage_references(model, platform::windows::path_identity_key);
    };
    unsigned api_calls=0;
    const auto run = [&](const char* phase, bool conflict, bool success=true) {
        ++api_calls;
        write(evidence / (std::string{phase}+".attempt.txt"), "mock LINK coordinator API; no compiler or external process\n");
        const auto before=runner.calls;
        auto value=linking.run_recorded(request);
        std::ostringstream facts;
        facts << "api=" << api_calls << "\nmock_runner_delta=" << runner.calls-before
              << "\nsuccess=" << bool(value) << '\n';
        if (value) {
            facts << "linked=" << value->result.linked << '\n';
            for (const auto& p : value->record.association.file_inputs) facts << "input=" << text(p) << '\n';
            for (const auto& p : value->record.association.side_outputs) facts << "side_output=" << text(p) << '\n';
        } else facts << "error=" << value.error().message << '\n';
        write(evidence / (std::string{phase}+".record.txt"), facts.str());
        if (runner.calls != before) {
            write(evidence / (std::string{phase}+".stdout.txt"), runner.stdout_text);
            std::ostringstream args; for (const auto& a : runner.arguments) args << "arg=" << a << '\n';
            write(evidence / (std::string{phase}+".argv.txt"), args.str());
        }
        if (fs::is_regular_file(request.cache_file))
            write(evidence / (std::string{phase}+".linkcache"), bytes(request.cache_file));
        require(bool(value)==success, "role fixture retains exact LINK success/failure");
        if (value) {
            require(contains(value->record.association.libraries, declared) &&
                    contains(value->record.association.file_inputs, dependency),
                    "declared and transitive input libraries are never removed by creation filtering");
            require(contains(value->record.association.side_outputs, import) &&
                    contains(value->record.association.side_outputs, exports), "DLL side outputs retained");
            const auto projected=project(*value);
            if (conflict) require(!projected &&
                projected.error().issue==RecordedStorageProjectionIssue::path_conflict &&
                !projected.error().source_index &&
                projected.error().message=="output or metadata aliases a protected input",
                "old or genuine dual-role input is still exactly refused");
            else require(projected.has_value(), "clean observed roles permit synthetic compile/LINK projection");
        }
        return value;
    };
    const auto cold=run("01-cold",false);
    require(cold->result.linked && runner.calls==1, "one cold mock LINK");
    const auto clean_bytes=bytes(request.cache_file);
    const auto warm=run("02-clean-reuse",false);
    require(!warm->result.linked && runner.calls==1 && bytes(request.cache_file)==clean_bytes,
            "clean reuse invokes no tool and rewrites no cache");
    auto legacy=cold->record.association;
    legacy.file_inputs.push_back(import);
    require(LinkCacheFile::save(request.cache_file,legacy).has_value(), "seed a separately labelled old-style cache fixture");
    const auto old_bytes=bytes(request.cache_file);
    write(evidence / "legacy-seeded.linkcache",old_bytes);
    const auto old=run("03-legacy-reuse",true);
    require(!old->result.linked && runner.calls==1 && bytes(request.cache_file)==old_bytes,
            "old role conflict is not silently sanitized on reuse");
    request.force_relink=true; runner.fail=true; runner.stdout_text.clear();
    const auto failed=run("04-forced-failure",true,false);
    require(failed.error().code==IncrementalLinkErrorCode::link_failed &&
            failed.error().linker_error && bytes(request.cache_file)==old_bytes && runner.calls==2,
            "failed repair retains the old cache and original LINK error");
    runner.fail=false;
    const auto repaired=run("05-forced-rebuild",false);
    require(repaired->result.linked && runner.calls==3 && bytes(request.cache_file)!=old_bytes &&
            contains(old->record.association.file_inputs,import),
            "only successful new LINK replaces old cache; prior returned record stays unchanged");
    request.force_relink=false;
    const auto reuse=run("06-repaired-reuse",false);
    require(!reuse->result.linked && runner.calls==3, "repaired cache reuses without LINK");
    write(dependency,"changed transitive library");
    fs::last_write_time(dependency,fs::last_write_time(output)+std::chrono::seconds{2});
    const auto changed=run("07-input-change",false);
    require(changed->result.linked && runner.calls==4 &&
            std::find(runner.arguments.begin(),runner.arguments.end(),"/INCREMENTAL:NO")!=runner.arguments.end(),
            "real observed input change still forces full LINK");
    require(fs::remove(exports), "remove only fixture export output");
    const auto side=run("08-side-repair",false);
    require(side->result.linked && runner.calls==5 && fs::is_regular_file(exports),
            "missing side output still invokes repair");
    request.force_relink=true; runner.read_import=true;
    const auto actual_conflict=run("09-genuine-read",true);
    require(actual_conflict->result.linked && contains(actual_conflict->record.association.file_inputs,import),
            "separate same-output read observation is not blacklisted");
    require(api_calls==9 && runner.calls==6, "registered nine APIs and six mock LINK entries, no retries");
    write(evidence / "completed.txt", "api_calls=9\nmock_link_calls=6\nexternal_processes=0\ncompiler_calls=0\nexpected_link_failures=1\nlegacy_rewritten_on_reuse=false\ndeletion_authorized=false\n");
    std::cout << "LINK role compatibility: 9 APIs, 6 mock LINK entries, legacy/genuine conflicts refused; no external process\n";
}
// END link observation role compatibility controls

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
        auto result = target_wave_cache_checks::recorded(target, request,
            evidence / phase, ArtifactGenerationLabel{"fixture", phase});
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
        if (result) {
            // Build completion and pure projection admission are separate.
            // The unchanged record/cache evidence above must survive refusal.
            const auto file_inputs = result->record.link.association.file_inputs;
            const auto side_outputs = result->record.link.association.side_outputs;
            const auto projected = project_storage_references(*result, platform::windows::path_identity_key);
            if (std::string_view{phase} == "07-dll") {
                const auto import_library = msvc::MsvcLinker::import_library_path(result->record.link.association.output);
                const auto import_key = platform::windows::path_identity_key(import_library);
                const auto contains_import = [&](const auto& paths) {
                    return std::any_of(paths.begin(), paths.end(), [&](const auto& path) {
                        return platform::windows::path_identity_key(path) == import_key;
                    });
                };
                const bool dual_role = contains_import(file_inputs) && contains_import(side_outputs);
                std::cout << "recorded storage projection phase=" << phase
                          << " import_library_input_and_output=" << dual_role << '\n';
                require(result->record.link.options.target_kind == TargetKind::dynamic_library &&
                        !contains_import(file_inputs) && contains_import(side_outputs),
                        "native clean DLL creation records import library only as a side output");
                if (!projected) throw std::runtime_error("native recorded storage projection: " + projected.error().message);
                require(projected->compiles.size() == result->cache_evidence.compiles.size(),
                        "clean DLL projects the original successful compile evidence");
                std::cout << "recorded storage projection phase=" << phase
                          << " result=accepted build_record_retained=true\n";
            } else {
                if (!projected) throw std::runtime_error("native recorded storage projection: " + projected.error().message);
                require(projected->compiles.size() == result->cache_evidence.compiles.size(),
                        "real target cache evidence projected without extra tool calls");
            }
            require(result->record.link.association.file_inputs == file_inputs &&
                    result->record.link.association.side_outputs == side_outputs,
                    "pure projection never removes conflicting historical input or output roles");
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
    target_wave_cache_checks::history(evidence / "01-cold", *cold);
    target_wave_cache_checks::history(evidence / "02-reuse", *warm);
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
        library_observation_role_cases(root / "link-role-model", work / "storage-evidence/link-role-model");
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
