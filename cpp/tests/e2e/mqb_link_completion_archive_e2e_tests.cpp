#include "mqb/orchestration/LinkCompletionArchive.hpp"
#include "mqb/orchestration/LinkFactSnapshotProjection.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/platform/windows/StorageFileObservation.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;
using namespace mqb;
using namespace mqb::orchestration;
using namespace mqb::platform::windows;
unsigned checks{};
std::ofstream journal;
void check(bool ok, std::string_view label) {
    ++checks;
    if (journal.is_open()) { journal << checks << ' ' << (ok ? "PASS " : "FAIL ") << label << '\n'; journal.flush(); }
    if (!ok) throw std::runtime_error(std::string{label});
}
#ifdef _WIN32
std::string text(const fs::path& p) { const auto v = p.generic_u8string(); return {v.begin(), v.end()}; }
void put(const fs::path& p, std::string_view value) {
    fs::create_directories(p.parent_path());
    std::ofstream out{p, std::ios::binary}; out.write(value.data(), static_cast<std::streamsize>(value.size())); out.close();
    if (out.fail()) throw std::runtime_error("test evidence write failed");
}
std::string bytes(const fs::path& p) {
    std::ifstream in{p, std::ios::binary};
    if (!in) throw std::runtime_error("test evidence read failed");
    std::string value{std::istreambuf_iterator<char>{in}, {}};
    if (in.bad()) throw std::runtime_error("test evidence read error");
    return value;
}
struct Handle { HANDLE value{INVALID_HANDLE_VALUE}; ~Handle() { if (value != INVALID_HANDLE_VALUE) ::CloseHandle(value); } };
struct Runner final : process::ProcessRunner {
    WindowsProcessRunner actual;
    fs::path evidence;
    std::string phase{"discovery"};
    unsigned calls{}, compiles{}, links{};
    explicit Runner(fs::path p) : evidence(std::move(p)) {}
    std::expected<process::ProcessResult, process::ProcessError> run(const process::ProcessSpec& spec) override {
        ++calls;
        if (phase == "compile") check(++compiles <= 1, "one source compile budget");
        else if (phase != "discovery") check(++links <= 3, "three link process budget");
        const auto prefix = evidence / (std::to_string(calls) + "-" + phase);
        std::ostringstream info; info << "executable=" << text(spec.executable) << '\n';
        for (const auto& arg : spec.arguments) info << "arg=" << arg << '\n';
        put(prefix.string() + ".started.txt", info.str());
        auto result = actual.run(spec);
        if (!result) put(prefix.string() + ".error.txt", result.error().message);
        else {
            put(prefix.string() + ".exit.txt", std::to_string(result->exit_code));
            put(prefix.string() + ".stdout.txt", result->stdout_text);
            put(prefix.string() + ".stderr.txt", result->stderr_text);
        }
        return result;
    }
};
void run() {
    const auto work = fs::current_path();
    const auto evidence = work / "storage-evidence" / "completion-archive";
    const auto root = work / "storage-fixtures" / fs::path{L"archive space \u65e5"};
    check(!fs::exists(evidence) && !fs::exists(root), "fresh retained test directories");
    fs::create_directories(evidence);
    journal.open(evidence / "checks.txt", std::ios::binary);
    check(journal.is_open(), "evidence journal opened");
    Runner runner{evidence / "processes"};
    put(root / "main.cpp", "int main(){return 23;}\n"); put(evidence / "input.cpp", bytes(root / "main.cpp"));
    msvc::MsvcToolchainLocator locator{runner};
    msvc::DiscoveryOptions discovery; discovery.preference = msvc::ToolchainPreference::visual_studio;
    discovery.cache_file = root / ".mqb/cache/toolchain/vs-x64.cache";
    auto tools = locator.discover(discovery);
    if (!tools) put(evidence / "discovery.error.txt", tools.error().message);
    check(tools.has_value(), "real MSVC toolchain discovered");
    auto layout = ProjectArtifactLayout::create(root); check(layout.has_value(), "fixture artifact layout");
    auto source = layout->for_source(root / "main.cpp"); auto target = layout->for_target("archived");
    check(source && target, "source and target layout");
    msvc::MsvcCompileExecutor executor{*tools, runner};
    MsvcIncrementalCompileCoordinator compiling{*tools, executor};
    IncrementalCompileRequest compile;
    compile.unit.source = root / "main.cpp"; compile.unit.outputs = {{source->object, ArtifactKind::object}};
    compile.cache_file = source->compile_cache; compile.source_dependencies_file = source->dependencies;
    compile.working_directory = root; runner.phase = "compile";
    auto compiled = compiling.run(compile);
    if (!compiled) put(evidence / "compile.error.txt", compiled.error().message);
    check(compiled && compiled->compiled, "one real source compiled");
    msvc::MsvcLinker linker{*tools, runner}; MsvcIncrementalLinkCoordinator linking{*tools, linker};
    IncrementalLinkRequest request;
    request.objects = {source->object}; request.output = target->executable; request.cache_file = target->link_cache;
    request.working_directory = root; request.options.additional_arguments = {"/INCREMENTAL:NO"};
    unsigned builds{}, observations{}, archives{};
    auto observe = [&](const fs::path& path) { ++observations; return observe_storage_file(path); };
    auto invoke = [&](const char* phase, const IncrementalLinkRequest& req) {
        ++builds; runner.phase = phase;
        put(evidence / (std::string{phase} + ".started.txt"), text(req.output));
        auto done = observe_link_completion(linking.run_recorded(req), observe);
        if (!done) put(evidence / (std::string{phase} + ".error.txt"), done.error().message);
        return done;
    };
    auto cold = invoke("cold", request);
    check(cold && cold->build.result.linked && cold->observation.state == StorageFileObservationState::observed, "real cold completion");
    const auto original_image = bytes(request.output), original_cache = bytes(request.cache_file);
    const auto image_time = fs::last_write_time(request.output), cache_time = fs::last_write_time(request.cache_file);
    put(evidence / "original-image.bin", original_image); put(evidence / "original-linkcache.bin", original_cache);
    auto archive = [&](const char* name, const ObservedLinkResult& completed, const fs::path& path,
                       std::optional<std::string_view> label = std::nullopt) {
        const auto before = capture_link_fact_snapshot(completed);
        check(before.has_value(), "original result can be recorded before archive");
        const auto process = completed.build.result.process;
        const auto process_calls = runner.calls, observer_calls = observations, build_calls = builds;
        ++archives; put(evidence / (std::string{name} + ".started.txt"), text(path));
        auto result = archive_link_completion(completed, path, label);
        std::ostringstream outcome;
        outcome << "processes_before=" << process_calls << "\nprocesses_after=" << runner.calls
            << "\nobservers_before=" << observer_calls << "\nobservers_after=" << observations
            << "\nbuilds_before=" << build_calls << "\nbuilds_after=" << builds << '\n';
        if (result) outcome << "stored=true\ndocument_bytes=" << result->document_bytes << '\n';
        else if (auto e = std::get_if<LinkFactFileError>(&result.error())) {
            outcome << "stored=false\nfile_error=" << static_cast<int>(e->code) << "\nstage=" << static_cast<int>(e->stage)
                << "\nnative_code=" << e->native_code << "\ntransferred=" << e->transferred_bytes << "\nfile_created=" << e->file_created << '\n';
        } else outcome << "stored=false\ncapture_error=" << std::get<LinkFactSnapshotError>(result.error()).message << '\n';
        put(evidence / (std::string{name} + ".result.txt"), outcome.str());
        const auto after = capture_link_fact_snapshot(completed);
        check(after && *after == *before, "original success reuse warnings and observation unchanged");
        check(process.has_value() == completed.build.result.process.has_value() && (!process ||
            (process->exit_code == completed.build.result.process->exit_code &&
             process->stdout_text == completed.build.result.process->stdout_text &&
             process->stderr_text == completed.build.result.process->stderr_text &&
             process->launch_duration == completed.build.result.process->launch_duration &&
             process->termination == completed.build.result.process->termination)), "original process result unchanged");
        check(runner.calls == process_calls && observations == observer_calls && builds == build_calls, "archive triggers zero build observer or tool calls");
        check(bytes(request.output) == original_image && bytes(request.cache_file) == original_cache &&
            fs::last_write_time(request.output) == image_time && fs::last_write_time(request.cache_file) == cache_time,
            "selected build artifacts bytes and timestamps unchanged");
        return result;
    };
    auto roundtrip = [&](const ObservedLinkResult& completed, const fs::path& path, const char* label) {
        auto expected = capture_link_fact_snapshot(completed, label);
        auto loaded = read_link_fact_snapshot_file(path);
        check(expected && loaded && *expected == *loaded, "stored exact completed snapshot reads back");
        auto encoded = encode_link_fact_snapshot(*expected);
        check(encoded && bytes(path) == *encoded && !loaded->producer_identity_verified && !loaded->current_content_verified &&
            !loaded->complete_producer_inventory && !loaded->deletion_authorized, "exact JSON bytes never gain authority");
    };
    const auto cold_path = evidence / "cold.json";
    check(archive("01-cold", *cold, cold_path, "cold").has_value(), "cold archive succeeds"); roundtrip(*cold, cold_path, "cold");
    const auto calls = runner.calls;
    auto warm = invoke("reuse", request);
    check(warm && !warm->build.result.linked && warm->build.record.completion == ArtifactCompletion::reused && runner.calls == calls,
        "reuse completed without another tool process");
    check(archive("02-reuse", *warm, evidence / "reuse.json", "reuse").has_value(), "reuse archive succeeds");
    roundtrip(*warm, evidence / "reuse.json", "reuse");
    const auto old_json = bytes(cold_path);
    auto conflict = archive("03-conflict", *warm, cold_path, "must not replace");
    check(!conflict && std::holds_alternative<LinkFactFileError>(conflict.error()) &&
        std::get<LinkFactFileError>(conflict.error()).code == LinkFactFileErrorCode::already_exists && bytes(cold_path) == old_json,
        "existing history never overwritten; build remains successful");
    auto absent = archive("04-missing-parent", *warm, evidence / "absent/record.json");
    check(!absent && std::holds_alternative<LinkFactFileError>(absent.error()) && !fs::exists(evidence / "absent"),
        "save failure does not create parent or trigger another build");
    std::string huge(LinkFactSnapshotLimits::string_bytes + 1, 'x');
    auto invalid = archive("05-capture-refused", *warm, evidence / "invalid.json", huge);
    check(!invalid && std::holds_alternative<LinkFactSnapshotError>(invalid.error()) && !fs::exists(evidence / "invalid.json"),
        "invalid capture never creates a history file");
    const auto blocked = evidence / "blocked"; fs::create_directory(blocked);
    {
        Handle hold{::CreateFileW(blocked.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
        check(hold.value != INVALID_HANDLE_VALUE, "controlled nonsharing directory handle");
        auto failed = archive("06-sharing", *warm, blocked / "record.json");
        check(!failed && std::holds_alternative<LinkFactFileError>(failed.error()) &&
            std::get<LinkFactFileError>(failed.error()).native_code == ERROR_SHARING_VIOLATION,
            "native sharing refusal remains a separate archive outcome");
    }
    check(!fs::exists(blocked / "record.json"), "failed save left no created leaf");
    auto warning_request = request; warning_request.output = root / ".mqb/bin/warning.exe";
    warning_request.cache_file = root / ".mqb/blocked-cache";
    put(warning_request.cache_file / "sentinel", "keep original cache obstruction");
    auto warned = invoke("warning", warning_request);
    check(warned && warned->build.record.cache_state == ArtifactCacheState::save_failed && !warned->build.result.warnings.empty(),
        "real cache warning completion retained");
    check(archive("07-warning", *warned, evidence / "warning.json", "warning").has_value(), "warning-bearing result archived");
    roundtrip(*warned, evidence / "warning.json", "warning");
    auto bad = request; bad.output = root / ".mqb/bin/failed.exe"; bad.cache_file = root / ".mqb/cache/link/failed.linkcache";
    bad.objects = {root / "bad.obj"}; put(bad.objects[0], "deliberately invalid COFF\n");
    const auto before_observations = observations;
    auto failed = invoke("failed-link", bad);
    if (failed) (void)archive("unexpected-success", *failed, evidence / "unexpected.json");
    check(!failed && failed.error().code == IncrementalLinkErrorCode::link_failed && observations == before_observations &&
        !fs::exists(evidence / "unexpected.json"), "original failed link produces no success to archive");
    check(builds == 4 && observations == 3 && archives == 7 && runner.compiles == 1 && runner.links == 3,
        "fixed real build observer and archive budgets complete");
    put(evidence / "completed.txt", "builds=4\nobservations=3\narchive_attempts=7\ncompiles=1\nlinks=3\nnew_builds_from_archive=0\ndelete_authority=false\n");
}
#endif
}
int main() {
#ifdef _WIN32
    try { run(); std::cout << "completion archive Windows: " << checks << " checks passed\n"; }
    catch (const std::exception& e) { std::cerr << "check " << checks << ": " << e.what() << '\n'; return 1; }
#else
    std::cerr << "Windows/MSVC completion archive test not executed\n"; return 2;
#endif
}
