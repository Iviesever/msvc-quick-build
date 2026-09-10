// End-to-end fixture for the explicit target API; no product/CLI policy change.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <optional>
#include <semaphore>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mqb/core/ProjectArtifactLayout.hpp"
#include "mqb/msvc/MsvcCompileExecutor.hpp"
#include "mqb/msvc/MsvcLinker.hpp"
#include "mqb/orchestration/MsvcIncrementalTargetCoordinator.hpp"
#include "mqb/platform/windows/CommandLine.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"

namespace {
namespace fs = std::filesystem;
namespace orch = mqb::orchestration;
using namespace std::chrono_literals;
using mqb::platform::windows::WindowsProcessRunner;
using NativeResult = std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>;
using TargetResult = std::expected<orch::IncrementalTargetResult, orch::IncrementalTargetError>;
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
std::string text(const fs::path& path) {
    auto result = mqb::platform::windows::utf16_to_utf8(path.generic_wstring());
    require(result.has_value(), "path encoding failed"); return *result;
}
std::string js(std::string_view value) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += static_cast<char>(c);
    }
    return out + '"';
}
std::string boolean(bool value) { return value ? "true" : "false"; }
void write(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary); out.write(bytes.data(), static_cast<std::streamsize>(bytes.size())); out.flush();
    require(bool(out), "cannot preserve " + text(path));
}
struct Handle {
    HANDLE value{};
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() { if (*this) ::CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};
std::uint64_t ticks(FILETIME t) { return (std::uint64_t{t.dwHighDateTime} << 32) | t.dwLowDateTime; }
std::string hash(std::string& bytes) {
    BCRYPT_ALG_HANDLE algorithm{};
    require(::BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0, "SHA256 provider failed");
    struct Close { BCRYPT_ALG_HANDLE h; ~Close() { ::BCryptCloseAlgorithmProvider(h, 0); } } close{algorithm};
    std::array<unsigned char, 32> output{};
    require(bytes.size() <= 64ULL * 1024 * 1024, "snapshot cap exceeded");
    require(::BCryptHash(algorithm, nullptr, 0, reinterpret_cast<PUCHAR>(bytes.data()),
            static_cast<ULONG>(bytes.size()), output.data(), static_cast<ULONG>(output.size())) == 0, "SHA256 failed");
    constexpr char hex[] = "0123456789abcdef"; std::string result;
    for (auto b : output) { result += hex[b >> 4]; result += hex[b & 15]; }
    return result;
}
// All snapshot handles close before a target run. A finite observation of bytes,
// mtime and file ID is NOT a lock, crash-recovery or all-writer certificate.
struct Snapshot {
    std::string bytes, digest;
    std::uint64_t modified{}, identity{}, volume{};
    bool operator==(const Snapshot& other) const {
        return bytes == other.bytes && modified == other.modified && identity == other.identity && volume == other.volume;
    }
    std::string json() const {
        return "{\"size\":" + std::to_string(bytes.size()) + ",\"sha256\":" + js(digest)
            + ",\"mtime\":" + std::to_string(modified) + ",\"file_id\":" + std::to_string(identity)
            + ",\"volume\":" + std::to_string(volume) + "}";
    }
};
Snapshot snapshot(const fs::path& path) {
    Handle file{::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    require(bool(file), "snapshot open failed: " + text(path) + " native=" + std::to_string(::GetLastError()));
    BY_HANDLE_FILE_INFORMATION before{}, after{};
    require(::GetFileInformationByHandle(file.value, &before) != FALSE, "snapshot identity failed");
    const auto size = (std::uint64_t{before.nFileSizeHigh} << 32) | before.nFileSizeLow;
    require(size > 0 && size <= 64ULL * 1024 * 1024, "snapshot size invalid");
    Snapshot result; result.bytes.resize(static_cast<std::size_t>(size));
    std::size_t offset = 0;
    while (offset < size) {
        DWORD read{};
        require(::ReadFile(file.value, result.bytes.data() + offset, static_cast<DWORD>(size - offset), &read, nullptr) && read != 0,
                "snapshot read incomplete");
        offset += read;
    }
    require(::GetFileInformationByHandle(file.value, &after) != FALSE && before.nFileSizeHigh == after.nFileSizeHigh
        && before.nFileSizeLow == after.nFileSizeLow && ticks(before.ftLastWriteTime) == ticks(after.ftLastWriteTime)
        && before.nFileIndexHigh == after.nFileIndexHigh && before.nFileIndexLow == after.nFileIndexLow,
        "snapshot changed while reading");
    result.modified = ticks(after.ftLastWriteTime);
    result.identity = (std::uint64_t{after.nFileIndexHigh} << 32) | after.nFileIndexLow;
    result.volume = after.dwVolumeSerialNumber; result.digest = hash(result.bytes);
    return result;
}
struct Child { DWORD pid{}; std::uint64_t created{}; Handle process; };
std::vector<Child> compilers(const fs::path& expected_image) {
    Handle snapshot{::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    require(bool(snapshot), "process snapshot failed");
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    std::vector<Child> result;
    auto more = ::Process32FirstW(snapshot.value, &entry);
    while (more) {
        if (entry.th32ParentProcessID == ::GetCurrentProcessId() && _wcsicmp(entry.szExeFile, L"cl.exe") == 0) {
            Handle process{::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID)};
            require(bool(process), "cannot retain observed compiler identity");
            FILETIME created{}, exited{}, kernel{}, user{};
            require(::GetProcessTimes(process.value, &created, &exited, &kernel, &user) != FALSE, "compiler creation time unavailable");
            std::wstring image(32768, L'\0'); DWORD size = static_cast<DWORD>(image.size());
            require(::QueryFullProcessImageNameW(process.value, 0, image.data(), &size) != FALSE, "compiler image unavailable");
            image.resize(size);
            require(_wcsicmp(image.c_str(), expected_image.wstring().c_str()) == 0, "unexpected compiler image");
            result.push_back(Child{entry.th32ProcessID, ticks(created), std::move(process)});
        }
        more = ::Process32NextW(snapshot.value, &entry);
    }
    require(::GetLastError() == ERROR_NO_MORE_FILES, "incomplete process enumeration");
    return result;
}
bool live(const Child& child) {
    const auto status = ::WaitForSingleObject(child.process.value, 0);
    require(status == WAIT_OBJECT_0 || status == WAIT_TIMEOUT, "retained process wait failed");
    return status == WAIT_TIMEOUT;
}

void save_process(const fs::path& stem, const NativeResult& result) {
    if (!result) {
        write(stem.string() + ".result.json", "{\"infrastructure_error\":true,\"native_code\":"
            + std::to_string(result.error().native_code) + ",\"message\":" + js(result.error().message) + "}\n");
        return;
    }
    write(stem.string() + ".stdout.txt", result->stdout_text);
    write(stem.string() + ".stderr.txt", result->stderr_text);
    write(stem.string() + ".result.json", "{\"exit_code\":" + std::to_string(result->exit_code)
        + ",\"cancelled\":" + boolean(result->termination == mqb::process::ProcessTermination::cancelled) + "}\n");
}
struct Runner final : mqb::process::ProcessRunner {
    WindowsProcessRunner native;
    fs::path compiler, linker, directory;
    std::array<fs::path, 3> sources;
    std::atomic<unsigned> compiles{0}, links{0}, runs{0};
    std::atomic<bool> saw_token{false};
    std::counting_semaphore<2> entered{0}, launch{0};
    bool synchronize{}; // Set only between fully joined target invocations.
    void phase(const fs::path& path, bool gated = false) {
        directory = path; synchronize = gated; compiles = 0; links = 0; runs = 0;
    }
    NativeResult run(const mqb::process::ProcessSpec& spec) override {
        if (spec.cancellation.stop_possible()) { saw_token = true; throw std::runtime_error("terminating process token rejected"); }
        std::string label;
        const bool compiling = _wcsicmp(spec.executable.c_str(), compiler.c_str()) == 0;
        unsigned count = 0;
        if (compiling) {
            count = compiles.fetch_add(1);
            for (unsigned i = 0; i != sources.size(); ++i) {
                if (std::find(spec.arguments.begin(), spec.arguments.end(), text(sources[i])) != spec.arguments.end())
                    label = "work" + std::to_string(i);
            }
            require(!label.empty(), "compiler source not in target request");
        } else if (_wcsicmp(spec.executable.c_str(), linker.c_str()) == 0) {
            label = "link"; require(links.fetch_add(1) == 0, "unexpected second target link");
        } else { label = "run"; require(runs.fetch_add(1) == 0, "unexpected second program run"); }
        const auto stem = directory / label;
        require(!fs::exists(stem.string() + ".argv.json"), "duplicate invocation; refusing to erase diagnostics");
        std::string argv = "[" + js(text(spec.executable));
        for (const auto& arg : spec.arguments) argv += ',' + js(arg);
        write(stem.string() + ".argv.json", argv + "]\n");
        if (compiling && synchronize && count < 2) { entered.release(); launch.acquire(); }
        auto result = native.run(spec); // Exactly the product ProcessSpec; no fabricated outcomes.
        save_process(stem, result); // Native diagnostics survive subsequent product/fixture errors.
        return result;
    }
};
const mqb::process::ProcessResult* original(const orch::IncrementalCompileError& error) {
    if (!error.compile_error || !error.compile_error->compiler_error
        || !error.compile_error->compiler_error->process_result) return nullptr;
    return &*error.compile_error->compiler_error->process_result;
}
std::string phase_json(const std::optional<orch::TargetCompilePhase>& phase) {
    if (!phase) return "null";
    const auto outcome = phase->outcome();
    std::string out = "{\"outcome\":" + js(outcome == orch::WorkBatchOutcome::succeeded ? "succeeded"
        : outcome == orch::WorkBatchOutcome::cancelled ? "cancelled" : "failed");
    if (phase->scheduling && *phase->scheduling) {
        const auto& s = **phase->scheduling;
        out += ",\"worker_count\":" + std::to_string(s.worker_count) + ",\"started_count\":" + std::to_string(s.started_count)
            + ",\"stop_requested\":" + boolean(s.stop_requested) + ",\"admission_stop_observed\":" + boolean(s.admission_stop_observed)
            + ",\"stopped_before_all_items\":" + boolean(s.stopped_before_all_items);
    }
    return out + '}';
}
std::string report_json(const TargetResult& result) {
    std::string out = "{\"succeeded\":" + boolean(result.has_value());
    if (result) {
        out += ",\"linked\":" + boolean(result->link.linked) + ",\"compiles\":[";
        for (std::size_t i = 0; i < result->compiles.size(); ++i) {
            if (i) out += ',';
            const auto& r = result->compiles[i];
            out += "{\"source\":" + js(text(r.source)) + ",\"compiled\":" + boolean(r.result.compiled)
                + ",\"warnings\":" + std::to_string(r.result.warnings.size()) + '}';
        }
        return out + "]}";
    }
    const auto& e = result.error();
    out += ",\"code\":" + std::to_string(static_cast<int>(e.code)) + ",\"message\":" + js(e.message)
        + ",\"source\":" + js(text(e.source));
    if (e.compile_error) {
        const auto* r = original(*e.compile_error);
        out += ",\"original_exit\":" + (r ? std::to_string(r->exit_code) : "null");
        if (r) out += ",\"original_stdout\":" + js(r->stdout_text) + ",\"original_stderr\":" + js(r->stderr_text);
    }
    out += ",\"waves\":[";
    if (e.admission) for (std::size_t w = 0; w < e.admission->waves.size(); ++w) {
        if (w) out += ',';
        const auto& wave = e.admission->waves[w];
        out += "{\"inspection\":" + phase_json(wave.inspection) + ",\"execution\":" + phase_json(wave.execution)
            + ",\"execution_sources\":[";
        for (std::size_t i = 0; i < wave.execution_sources.size(); ++i) {
            if (i) out += ',';
            out += std::to_string(wave.execution_sources[i]);
        }
        out += "],\"attempts\":[";
        for (std::size_t i = 0; i < wave.attempts.size(); ++i) {
            if (i) out += ',';
            const auto& attempt = wave.attempts[i];
            if (!attempt) out += "{\"state\":\"not_started\"}";
            else if (*attempt) out += "{\"state\":\"succeeded\",\"compiled\":" + boolean(attempt->value().compiled) + '}';
            else {
                const auto* r = original(attempt->error());
                out += "{\"state\":\"failed\",\"exit_code\":" + (r ? std::to_string(r->exit_code) : "null") + '}';
            }
        }
        out += "]}";
    }
    return out + "]}";
}
std::string source(unsigned index, bool revised, bool fail) {
    std::string out = "#include \"shared.hpp\"\n";
    if (index < 2) for (unsigned f = 0; f < 12000; ++f) {
        const auto id = std::to_string(index) + "_" + std::to_string(f);
        out += "struct T" + id + "{int a,b;};\n__declspec(noinline) int f" + id
            + "(int x){T" + id + " t{x,x+1};return t.a+t.b;}\n";
    }
    if (index == 0) out += "int helper(); int pending(); int main(){return helper()==VALUE && pending()==1?0:1;}\n";
    if (index == 1) out += "int helper(){return VALUE;}\n";
    if (index == 2) out += "int pending(){return 1;}\n";
    // Revision changes the source bytes/size, not compiler policy or shared inputs.
    if (revised) out += "\n// fixed source revision for admission test\n";
    if (index == 1 && fail) out += "static_assert(false, \"MQB_TARGET_EXPECTED_COMPILER_FAILURE\");\n";
    return out;
}
int run(int argc, wchar_t** argv) {
    require(argc == 4, "use ROOT Debug|Release complete|cancel|failure-cancel");
    wchar_t authorization[2]{};
    require(::GetEnvironmentVariableW(L"MQB_TARGET_DISPOSABLE_HOST", authorization, 2) == 1 && authorization[0] == L'1',
        "target fixture requires an explicitly disposable host");
    const fs::path root = fs::absolute(argv[1]);
    require(!fs::exists(root), "refuse to overwrite previous evidence");
    const std::wstring configuration = argv[2], mode = argv[3];
    require(configuration == L"Debug" || configuration == L"Release", "unknown configuration");
    require(mode == L"complete" || mode == L"cancel" || mode == L"failure-cancel", "unknown mode");
    const bool stopping = mode != L"complete", failing = mode == L"failure-cancel";
    fs::create_directories(root / "src");
    for (const auto* phase : {"cold", "warm", "subject", "snapshots"}) fs::create_directories(root / "evidence" / phase);
    WindowsProcessRunner discovery_runner;
    mqb::msvc::DiscoveryOptions discovery; discovery.preference = mqb::msvc::ToolchainPreference::visual_studio;
    discovery.cache_file = fs::path{};
    auto found = mqb::msvc::MsvcToolchainLocator{discovery_runner}.discover(discovery);
    require(found.has_value(), found ? "" : found.error().message);
    auto tc = std::move(*found);
    for (const char* name : {"CL", "_CL_", "LINK", "_LINK_"}) {
        std::erase_if(tc.environment, [&](const auto& e) { return _stricmp(e.name.c_str(), name) == 0; });
        tc.environment.push_back({name, "", true});
    }
    write(root / "toolchain.txt", text(tc.identity.compiler) + '\n' + tc.identity.version + '\n' + tc.identity.binary_stamp + '\n');
    Runner runner; runner.compiler = tc.identity.compiler; runner.linker = tc.linker;
    mqb::msvc::MsvcCompileExecutor executor{tc, runner};
    orch::MsvcIncrementalCompileCoordinator compiling{tc, executor};
    mqb::msvc::MsvcLinker linker{tc, runner};
    orch::MsvcIncrementalLinkCoordinator linking{tc, linker};
    orch::MsvcIncrementalTargetCoordinator target{compiling, linking};
    auto layout = mqb::ProjectArtifactLayout::create(root); require(layout.has_value(), "artifact layout failed");
    orch::IncrementalTargetRequest request;
    request.working_directory = root; request.max_parallel_compiles = 2;
    request.compiler_options.configuration = configuration == L"Debug" ? mqb::BuildConfiguration::debug : mqb::BuildConfiguration::release;
    request.compiler_options.runtime_library = configuration == L"Debug" ? mqb::RuntimeLibrary::mtd : mqb::RuntimeLibrary::mt;
    request.compiler_options.additional_arguments = {"/Zi", "/FS", "/bigobj", "/Fd" + text(root / ".mqb/compiler.pdb")};
    request.link_options.configuration = request.compiler_options.configuration;
    // Keep real Debug incremental-link defaults; no linker policy workaround.
    auto artifacts = layout->for_target("target-admission"); require(artifacts.has_value(), "target layout failed");
    request.target = *artifacts;
    write(root / "src/shared.hpp", "#pragma once\n#define VALUE 42\n");
    for (unsigned i = 0; i != 3; ++i) {
        runner.sources[i] = root / "src" / ("work" + std::to_string(i) + ".cpp");
        write(runner.sources[i], source(i, false, false));
        write(root / "evidence/cold" / ("work" + std::to_string(i) + ".input.cpp"), source(i, false, false));
        auto a = layout->for_source(runner.sources[i]); require(a.has_value(), "source layout failed");
        request.sources.push_back({runner.sources[i], *a});
    }
    std::array<fs::path, 5> files{request.target.link_cache, request.target.executable,
        request.sources[0].artifacts.compile_cache, request.sources[1].artifacts.compile_cache, request.sources[2].artifacts.compile_cache};
    const std::array<const char*, 5> names{"link_cache", "executable", "source0_cache", "source1_cache", "source2_cache"};
    const auto snapshots = [&](const std::string& phase) {
        std::array<Snapshot, 5> values;
        std::string json = "{";
        for (std::size_t i = 0; i < files.size(); ++i) {
            values[i] = snapshot(files[i]);
            if (i) json += ',';
            json += js(names[i]) + ':' + values[i].json();
            if (i != 1) write(root / "evidence/snapshots" / (phase + "-" + names[i] + ".bytes"), values[i].bytes);
        }
        write(root / "evidence/snapshots" / (phase + ".json"), json + "}\n");
        return values;
    };
    const auto run_program = [&] {
        mqb::process::ProcessSpec program; program.executable = request.target.executable; program.working_directory = root;
        auto result = runner.run(program);
        require(result && result->exit_code == 0, "actual target program failed");
    };
    const auto run_phase = [&](const std::string& phase, std::stop_token stop) {
        auto result = target.run_with_compile_admission_stop(request, stop);
        write(root / "evidence" / phase / "target.json", report_json(result) + '\n');
        return result;
    };
    std::stop_source cold_stop;
    runner.phase(root / "evidence/cold");
    auto cold = run_phase("cold", cold_stop.get_token());
    require(cold && cold->any_compiled && cold->link.linked && runner.compiles == 3 && runner.links == 1, "cold target control failed");
    run_program(); const auto initial = snapshots("cold");
    runner.phase(root / "evidence/warm");
    std::stop_source warm_stop; auto warm = run_phase("warm", warm_stop.get_token());
    require(warm && !warm->any_compiled && !warm->link.linked && runner.compiles == 0 && runner.links == 0,
        "warm target unexpectedly dispatched work");
    const auto before = snapshots("before"); require(initial == before, "warm no-op changed cached artifacts");
    for (unsigned i = 0; i != 3; ++i) {
        write(runner.sources[i], source(i, true, failing));
        write(root / "evidence/subject" / ("work" + std::to_string(i) + ".input.cpp"), source(i, true, failing));
    }
    runner.phase(root / "evidence/subject", true);
    std::stop_source stop;
    auto future = std::async(std::launch::async, [&] { return run_phase("subject", stop.get_token()); });
    const bool first = runner.entered.try_acquire_for(10s), second = runner.entered.try_acquire_for(10s);
    runner.launch.release(2); // Always release callbacks even when an observation fails.
    bool overlap = false; std::string observation_error; std::vector<Child> active;
    try {
        const auto deadline = std::chrono::steady_clock::now() + 15s;
        do {
            active = compilers(tc.identity.compiler);
            if (active.size() == 2 && live(active[0]) && live(active[1])) { overlap = true; break; }
            if (future.wait_for(2ms) == std::future_status::ready) break;
        } while (std::chrono::steady_clock::now() < deadline);
    } catch (const std::exception& e) { observation_error = e.what(); }
    if (stopping || !overlap || !first || !second || !observation_error.empty()) stop.request_stop();
    auto result = future.get(); // Real target returns after its admitted compile callbacks join.
    bool exited = true; for (const auto& p : active) exited = exited && !live(p);
    const auto after = snapshots("after");
    std::array<bool, 3> reusable{};
    for (unsigned i = 0; i < 3; ++i) {
        orch::IncrementalCompileRequest inspect;
        inspect.unit.source = runner.sources[i]; inspect.unit.outputs.push_back({request.sources[i].artifacts.object, mqb::ArtifactKind::object});
        inspect.options = request.compiler_options; inspect.cache_file = request.sources[i].artifacts.compile_cache;
        inspect.source_dependencies_file = request.sources[i].artifacts.dependencies;
        auto r = compiling.inspect(inspect);
        require(r.has_value(), "post-target read-only source inspection failed");
        reusable[i] = r->plan.empty();
    }
    orch::IncrementalLinkRequest link_inspection;
    for (const auto& item : request.sources) link_inspection.objects.push_back(item.artifacts.object);
    link_inspection.output = request.target.executable; link_inspection.options = request.link_options;
    link_inspection.cache_file = request.target.link_cache; link_inspection.working_directory = root;
    const auto link_plan = linking.inspect(link_inspection);
    require(link_plan.has_value(), "post-target read-only link inspection failed");
    const bool link_reusable = link_plan->plan.empty();
    // Inspection above must not mutate the objects/caches it inspected.
    const auto inspected = snapshots("inspected");
    bool good = first && second && overlap && exited && observation_error.empty() && after == inspected && !runner.saw_token;
    if (!stopping) {
        good = good && result && result->link.linked && runner.compiles == 3 && runner.links == 1
            && std::all_of(reusable.begin(), reusable.end(), [](bool v) { return v; })
            && link_reusable && result->link.warnings.empty() && !(before[0] == after[0]);
        if (result && result->link.linked) run_program();
        good = good && runner.runs == 1;
    } else {
        good = good && !result && result.error().code == (failing ? orch::IncrementalTargetErrorCode::compile_failed : orch::IncrementalTargetErrorCode::cancelled)
            && runner.compiles == 2 && runner.links == 0 && runner.runs == 0
            && before[0] == after[0] && before[1] == after[1] && before[4] == after[4]
            && reusable[0] && reusable[1] == !failing && !reusable[2] && !link_reusable;
        if (failing) good = good && before[3] == after[3];
        if (!result && result.error().admission && result.error().admission->waves.size() == 1) {
            const auto& w = result.error().admission->waves[0];
            good = good && w.inspection && w.inspection->all_succeeded() && w.execution
                && w.execution->scheduling && *w.execution->scheduling
                && (*w.execution->scheduling)->started_count == 2 && (*w.execution->scheduling)->admission_stop_observed
                && w.execution_sources == std::vector<std::size_t>{0,1,2}
                && w.attempts.size() == 3 && !w.attempts[2];
            if (failing && result.error().compile_error) {
                const auto* r = original(*result.error().compile_error);
                good = good && r && r->exit_code != 0 && result.error().source == runner.sources[1]
                    && (r->stdout_text + r->stderr_text).find("MQB_TARGET_EXPECTED_COMPILER_FAILURE") != std::string::npos;
            }
        } else good = false;
    }
    std::string children = "[";
    for (const auto& child : active) {
        if (children.size() > 1) children += ',';
        children += "{\"pid\":" + std::to_string(child.pid) + ",\"created\":" + std::to_string(child.created) + '}';
    }
    children += ']';
    write(root / "observation.json", "{\"schema\":1,\"configuration\":" + js(text(configuration)) + ",\"mode\":" + js(text(mode))
        + ",\"gate_passed\":" + boolean(good) + ",\"compiler_invocations\":" + std::to_string(runner.compiles.load())
        + ",\"link_invocations\":" + std::to_string(runner.links.load()) + ",\"run_invocations\":" + std::to_string(runner.runs.load())
        + ",\"overlap\":" + boolean(overlap) + ",\"observed_compilers\":" + children + ",\"retained_exited\":" + boolean(exited)
        + ",\"observation_error\":" + js(observation_error) + ",\"process_token_seen\":" + boolean(runner.saw_token)
        + ",\"post_reusable\":[" + boolean(reusable[0]) + ',' + boolean(reusable[1]) + ',' + boolean(reusable[2]) + ']'
        + ",\"post_link_reusable\":" + boolean(link_reusable)
        + ",\"target_api\":\"run_with_compile_admission_stop\",\"real_processes\":true,\"cli_integrated\":false"
          ",\"safe_to_transfer_write_lease\":false,\"historical_cause_resolved\":false}\n");
    return good ? 0 : 1;
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    try { return run(argc, argv); }
    catch (const std::exception& error) { std::cerr << "TARGET_ADMISSION_FIXTURE_ERROR " << error.what() << '\n'; return 1; }
}
