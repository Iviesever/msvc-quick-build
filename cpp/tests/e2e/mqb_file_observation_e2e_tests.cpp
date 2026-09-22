#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "mqb/core/StorageFileObservation.hpp"
#ifdef _WIN32
#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"
#include "mqb/orchestration/MsvcIncrementalLinkCoordinator.hpp"
#include "mqb/platform/windows/StorageInventory.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"
#include "mqb/platform/windows/CommandLine.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;
using namespace mqb;
using State = StorageFileObservationState;
void require(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string{message});
}
void model_contracts() {
    static_assert(!StorageFileObservation::producer_identity_verified);
    static_assert(!StorageFileObservation::current_content_verified);
    static_assert(!StorageFileObservation::complete_producer_inventory);
    static_assert(!StorageFileObservation::deletion_authorized);
    StorageFileObservation first;
    require(first.state == State::not_attempted && first.physical_id.empty() &&
        !first.logical_bytes && !first.hard_links, "unknown metadata is not zero or success");
    first.requested_path = "original.exe";
    first.state = State::observed;
    first.physical_id = "synthetic-observation-only";
    first.logical_bytes = 0;
    first.issues.push_back({"original.exe", "retained note", 0});
    auto later = first;
    later.requested_path = "changed.exe";
    later.physical_id = "different";
    later.issues[0].message = "changed";
    require(first.requested_path == fs::path{"original.exe"} && first.logical_bytes == 0 &&
        first.physical_id == "synthetic-observation-only" && first.issues[0].message == "retained note",
        "observation owns its values; present zero differs from missing");
    std::cout << "portable file-observation value contracts passed\n";
}
#ifdef _WIN32
using namespace mqb::orchestration;
std::string text(const fs::path& path) {
    auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
void write(const fs::path& path, std::string_view value) {
    fs::create_directories(path.parent_path());
    std::ofstream out{path, std::ios::binary};
    out << value; out.close();
    require(static_cast<bool>(out), "write fixture/evidence");
}
std::string bytes(const fs::path& path) {
    std::ifstream in{path, std::ios::binary};
    require(in.is_open(), "open retained fixture bytes");
    std::string value{std::istreambuf_iterator<char>{in}, {}};
    require(!in.bad(), "read retained fixture bytes");
    return value;
}
void save_observation(const fs::path& file, const StorageFileObservation& o) {
    std::ostringstream out;
    out << "requested_path=" << text(o.requested_path) << "\nstate=" << static_cast<int>(o.state)
        << "\nphysical_id=" << o.physical_id << "\nopened_components=" << o.opened_components << '\n';
    if (o.logical_bytes) out << "logical_bytes=" << *o.logical_bytes << '\n';
    if (o.allocated_bytes) out << "allocated_bytes=" << *o.allocated_bytes << '\n';
    if (o.hard_links) out << "hard_links=" << *o.hard_links << '\n';
    out << "producer_identity_verified=false\ncurrent_content_verified=false\n"
        << "complete_producer_inventory=false\ndeletion_authorized=false\n";
    for (const auto& issue : o.issues)
        out << "issue=" << text(issue.path) << '|' << issue.native_code << '|' << issue.message << '\n';
    write(file, out.str());
}
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~Handle() { if (value != INVALID_HANDLE_VALUE) ::CloseHandle(value); }
};
void native_contracts(const fs::path& root, const fs::path& evidence) {
    struct Runner final : process::ProcessRunner {
        platform::windows::WindowsProcessRunner actual;
        fs::path evidence;
        std::string phase{"discovery"};
        unsigned calls{}, compiles{}, links{}, programs{}, junctions{};
        explicit Runner(fs::path p) : evidence(std::move(p)) {}
        std::expected<process::ProcessResult, process::ProcessError> run(const process::ProcessSpec& spec) override {
            ++calls;
            if (phase == "compile") require(++compiles <= 1, "one source compiler invocation");
            else if (phase == "program") require(++programs <= 1, "one successful program");
            else if (phase == "junction") require(++junctions <= 1, "one real junction command");
            else if (phase != "discovery") require(++links <= 3, "three linker invocations maximum");
            auto prefix = evidence / (std::to_string(calls) + "-" + phase);
            if (phase != "discovery") {
                std::ostringstream out;
                out << "executable=" << text(spec.executable) << '\n';
                if (spec.working_directory) out << "cwd=" << text(*spec.working_directory) << '\n';
                for (const auto& arg : spec.arguments) out << "arg=" << arg << '\n';
                write(prefix.string() + ".argv.txt", out.str());
            }
            auto result = actual.run(spec);
            if (phase != "discovery") {
                if (!result) write(prefix.string() + ".error.txt", result.error().message);
                else {
                    write(prefix.string() + ".exit.txt", std::to_string(result->exit_code));
                    write(prefix.string() + ".stdout.txt", result->stdout_text);
                    write(prefix.string() + ".stderr.txt", result->stderr_text);
                }
            }
            return result;
        }
    } runner{evidence / "processes"};
    write(root / "main.cpp", "int main(){return 23;}\n");
    write(evidence / "input.cpp", bytes(root / "main.cpp"));
    msvc::MsvcToolchainLocator locator{runner};
    msvc::DiscoveryOptions discovery;
    discovery.preference = msvc::ToolchainPreference::visual_studio;
    discovery.cache_file = root / ".mqb/cache/toolchain/vs-x64.cache";
    auto toolchain = locator.discover(discovery);
    if (!toolchain) write(evidence / "discovery.error.txt", toolchain.error().message);
    require(toolchain.has_value(), "native MSVC discovery");
    auto layout = ProjectArtifactLayout::create(root);
    require(layout.has_value(), "fixture layout");
    auto source = layout->for_source(root / "main.cpp");
    auto target = layout->for_target("observed");
    require(source && target, "fixture artifact layout");
    msvc::MsvcCompileExecutor executor{*toolchain, runner};
    MsvcIncrementalCompileCoordinator compiling{*toolchain, executor};
    IncrementalCompileRequest compile;
    compile.unit.source = root / "main.cpp";
    compile.unit.outputs = {{source->object, ArtifactKind::object}};
    compile.cache_file = source->compile_cache;
    compile.source_dependencies_file = source->dependencies;
    compile.working_directory = root;
    runner.phase = "compile";
    auto compiled = compiling.run(compile);
    if (!compiled) write(evidence / "compile.error.txt", compiled.error().message);
    require(compiled && compiled->compiled, "single actual compile before terminal observations");
    msvc::MsvcLinker linker{*toolchain, runner};
    MsvcIncrementalLinkCoordinator linking{*toolchain, linker};
    IncrementalLinkRequest request;
    request.objects = {source->object};
    request.output = target->executable;
    request.cache_file = target->link_cache;
    request.working_directory = root;
    request.options.additional_arguments = {"/INCREMENTAL:NO"};
    unsigned link_calls{}, observations{};
    auto observe = [&](const fs::path& path) {
        require(++observations <= 20, "at most20 single-file observations");
        auto o = platform::windows::observe_storage_file(path);
        save_observation(evidence / ("observe-" + std::to_string(observations) + ".txt"), o);
        return o;
    };
    auto invoke = [&](const char* phase, const IncrementalLinkRequest& r, const StoragePathObserver& observer) {
        require(++link_calls <= 8, "fixed eight terminal calls"); runner.phase = phase;
        std::ostringstream attempt;
        attempt << "output=" << text(r.output) << "\ncache=" << text(r.cache_file) << '\n';
        for (const auto& object : r.objects) attempt << "object=" << text(object) << '\n';
        write(evidence / (std::string{phase} + ".attempt.txt"), attempt.str());
        auto result = linking.run_observed(r, observer);
        if (!result) write(evidence / (std::string{phase} + ".error.txt"),
            std::to_string(static_cast<int>(result.error().code)) + "\n" + result.error().message);
        else {
            const auto& rec = result->build.record;
            std::ostringstream out;
            out << "build_success=true\nobserver_invoked=" << result->observer_invoked
                << "\nlinked=" << result->build.result.linked << "\ncompletion=" << static_cast<int>(rec.completion)
                << "\ncache_state=" << static_cast<int>(rec.cache_state) << "\nsignature=" << rec.association.signature.hex()
                << "\noutput=" << text(rec.association.output) << "\ncache=" << text(rec.cache_file) << '\n';
            for (const auto& warning : result->build.result.warnings)
                out << "warning=" << static_cast<int>(warning.code) << '|' << warning.message << '\n';
            write(evidence / (std::string{phase} + ".record.txt"), out.str());
            save_observation(evidence / (std::string{phase} + ".observation.txt"), result->observation);
        }
        return result;
    };
    const auto cold = invoke("01-cold", request, observe);
    require(cold && cold->build.result.linked && cold->observation.state == State::observed &&
        !cold->observation.physical_id.empty(), "actual cold link followed by handle identity observation");
    const auto original = bytes(request.output);
    const auto stamp = fs::last_write_time(request.output);
    const auto link_cache = bytes(request.cache_file);
    const auto calls_before = runner.calls;
    const auto warm = invoke("02-reuse", request, observe);
    require(warm && !warm->build.result.linked && warm->build.record.completion == ArtifactCompletion::reused &&
        warm->observation.state == State::observed && warm->observation.physical_id == cold->observation.physical_id &&
        runner.calls == calls_before && bytes(request.output) == original && fs::last_write_time(request.output) == stamp &&
        bytes(request.cache_file) == link_cache, "reuse has no tools and observation preserves selected bytes/time");
    {
        Handle released{::CreateFileW(request.output.c_str(), GENERIC_WRITE | DELETE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr)};
        write(evidence / "after-return-handle.txt", std::to_string(released.value == INVALID_HANDLE_VALUE ? ::GetLastError() : 0));
        require(released.value != INVALID_HANDLE_VALUE, "observer returns without retained blocking leaf handles");
    }
    runner.phase = "program";
    process::ProcessSpec program;
    program.executable = request.output; program.working_directory = root;
    program.capture_stdout = program.capture_stderr = true;
    auto ran = runner.run(program);
    require(ran && ran->exit_code == 23, "consumer runs before any deliberate mutation");
    bool writer_opened = false;
    const auto busy = invoke("03-busy", request, [&](const fs::path& path) {
        Handle writer{::CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr)};
        writer_opened = writer.value != INVALID_HANDLE_VALUE;
        write(evidence / "writer.txt", std::to_string(writer_opened ? 0 : ::GetLastError()));
        require(writer_opened, "open controlled writer after link reuse before observation");
        return observe(path);
    });
    require(writer_opened && busy && !busy->build.result.linked && busy->observation.state == State::unavailable &&
        busy->observation.physical_id.empty(), "successful build with separately unavailable observation");
    const auto thrown = invoke("04-observer-error", request, [](const fs::path&) -> StorageFileObservation {
        throw std::runtime_error("OBSERVER_EXPECTED_FAILURE");
    });
    require(thrown && thrown->observer_invoked && thrown->observation.state == State::unavailable &&
        thrown->observation.issues.size() == 1, "observer exception does not erase successful build");
    const auto empty = invoke("05-no-observer", request, {});
    require(empty && !empty->observer_invoked && empty->observation.state == State::not_attempted,
        "empty callback is not a successful observation");
    const auto saved = request.output.parent_path() / "retained-original.exe";
    const auto missing = invoke("06-gap-removed", request, [&](const fs::path& path) {
        fs::rename(path, saved); // Controlled intervention in the post-build/pre-open gap.
        return observe(path);
    });
    require(missing && missing->observation.state == State::missing_leaf && fs::exists(saved) &&
        !fs::exists(request.output), "build success does not promise output survives until observer open");
    auto warning_request = request;
    warning_request.output = request.output.parent_path() / "warning.exe";
    warning_request.cache_file = root / ".mqb/blocked-cache";
    write(warning_request.cache_file / "sentinel", "keep obstruction");
    const auto warned = invoke("07-cache-warning", warning_request, observe);
    require(warned && warned->build.record.cache_state == ArtifactCacheState::save_failed &&
        !warned->build.result.warnings.empty() && warned->observation.state == State::observed,
        "observed main output does not promote failed cache save to saved");
    auto bad = request;
    bad.output = root / ".mqb/bin/failed.exe";
    bad.cache_file = root / ".mqb/cache/link/failed.linkcache";
    bad.objects = {root / "bad.obj"}; write(bad.objects[0], "deliberately invalid COFF object\n");
    const auto before_failure = observations;
    const auto failed = invoke("08-link-failed", bad, observe);
    require(!failed && failed.error().code == IncrementalLinkErrorCode::link_failed &&
        observations == before_failure && !fs::exists(evidence / "08-link-failed.record.txt"),
        "original link error: no observer, no successful record, no retry");

    auto retained = observe(saved);
    require(retained.state == State::observed && retained.physical_id == cold->observation.physical_id,
        "renamed original remains a separate finite file-ID observation");
    write(request.output, "replacement is not the successful link's image");
    auto replaced = observe(request.output);
    require(replaced.state == State::observed && replaced.physical_id != retained.physical_id &&
        cold->observation.physical_id == retained.physical_id && cold->observation.logical_bytes == original.size(),
        "same path replacement cannot mutate historical observation or prove provenance");
    const auto alias = saved.parent_path() / "alias.exe";
    const bool hardlink = ::CreateHardLinkW(alias.c_str(), saved.c_str(), nullptr) != FALSE;
    write(evidence / "hardlink.txt", std::to_string(hardlink ? 0 : ::GetLastError()));
    require(hardlink, "real hardlink fixture");
    const auto left = observe(saved), right = observe(alias);
    require(left.state == State::observed && right.state == State::observed && left.physical_id == right.physical_id &&
        left.hard_links == 2 && right.hard_links == 2, "two names are observed, not exclusive ownership");
    require(observe(root / "absent-parent/file.exe").state == State::unavailable, "missing parent is not missing leaf");
    require(observe(root / ".mqb/bin").state == State::not_regular_file, "directory is not a terminal file");

    const auto external = evidence / "outside";
    write(external / "sentinel.txt", "outside must not be observed through junction");
    auto native_text = [](fs::path p) {
        p.make_preferred(); auto value = platform::windows::utf16_to_utf8(p.wstring());
        require(value.has_value(), "encode native launch path"); return *value;
    };
    wchar_t system[32768]{};
    const auto length = ::GetSystemDirectoryW(system, 32768);
    write(evidence / "system-directory.txt", std::to_string(length) + "\n" + std::to_string(length ? 0 : ::GetLastError()));
    require(length && length < 32768, "system command directory");
    const auto junction_path = root / ".mqb/junction";
    process::ProcessSpec junction;
    junction.executable = fs::path{system} / L"cmd.exe"; junction.executable.make_preferred();
    junction.working_directory = root; junction.working_directory->make_preferred();
    junction.arguments = {"/d", "/c", "mklink", "/J", native_text(junction_path), native_text(external)};
    junction.capture_stdout = junction.capture_stderr = true;
    runner.phase = "junction";
    auto made = runner.run(junction);
    require(made && made->exit_code == 0 && fs::equivalent(junction_path, external), "real junction fixture");
    require(observe(junction_path).state == State::reparse_rejected, "final junction refused");
    require(observe(junction_path / "sentinel.txt").state == State::reparse_rejected &&
        bytes(external / "sentinel.txt") == "outside must not be observed through junction", "reparse ancestor not traversed");
    auto nul = (root / "nul-byte").native(); nul.push_back(L'\0'); nul += L"suffix";
    const std::vector<fs::path> invalid = {fs::path{"relative.exe"}, fs::path{nul}, root / "a.exe:stream",
        root / "../outside.exe", fs::path{L"\\\\server\\share\\file.exe"}, root / L"NUL"};
    for (const auto& path : invalid) {
        const auto o = observe(path);
        require(o.state == State::invalid_path && o.opened_components == 0 && o.physical_id.empty(),
            "invalid paths refused before any native open");
    }
    auto deep = root;
    for (unsigned i = 0; i < 129; ++i) deep /= "component";
    auto too_deep = observe(deep / "leaf.exe");
    require(too_deep.state == State::limit_exceeded && too_deep.opened_components == 0, "depth budget refuses before IO");
    require(link_calls == 8 && observations == 20 && runner.compiles == 1 && runner.links == 3 &&
        runner.programs == 1 && runner.junctions == 1, "fixed terminal/native observation budgets completed");
    write(evidence / "completed.txt", "8 terminal calls; 7 successes/1 link failure; 1 compiler/3 linkers; 20 file observations; 1 program/1 junction; no deletion authority\n");
}
#endif
} // namespace
int main() {
    try {
        model_contracts();
#ifdef _WIN32
        const auto work = fs::current_path();
        require(!fs::exists(work / "storage-fixtures") && !fs::exists(work / "storage-evidence"), "fresh test tree required");
        native_contracts(work / "storage-fixtures" / fs::path{L"file observation space \u65e5"}, work / "storage-evidence/file-observations");
#else
        std::cout << "Windows handles, terminal callbacks and MSVC NOT executed\n";
#endif
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
