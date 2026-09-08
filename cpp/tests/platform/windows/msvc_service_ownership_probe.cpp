// Diagnostic-only M1b experiment. Never used by the production CLI.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <restartmanager.h>

#include <algorithm>
#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "mqb/msvc/MsvcToolchainLocator.hpp"
#include "mqb/platform/windows/CommandLine.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"

namespace {
namespace fs = std::filesystem;
using mqb::platform::windows::WindowsProcessRunner;
using mqb::process::ProcessSpec;
using mqb::process::ProcessResult;
using RunResult = std::expected<ProcessResult, mqb::process::ProcessError>;
using mqb::msvc::MsvcToolchain;
using namespace std::chrono_literals;

struct Handle {
    HANDLE value{};
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() { if (*this) ::CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& h) noexcept : value(std::exchange(h.value, nullptr)) {}
    Handle& operator=(Handle&& h) noexcept {
        if (this != &h) {
            if (*this) ::CloseHandle(value);
            value = std::exchange(h.value, nullptr);
        }
        return *this;
    }
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};

void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
std::string utf8(const std::wstring& s) {
    auto result = mqb::platform::windows::utf16_to_utf8(s);
    require(result.has_value(), "UTF-16 conversion failed");
    return *result;
}
std::string path_text(const fs::path& p) { return utf8(p.wstring()); }
fs::path self() {
    std::wstring buffer(32768, L'\0');
    auto n = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    require(n > 0 && n < buffer.size(), "GetModuleFileName failed");
    buffer.resize(n);
    return fs::path{buffer};
}
void write(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
    out.flush();
    require(bool(out), "cannot write " + path_text(path));
}
std::string quoted(const std::string& value) {
    std::string result = "\"";
    for (unsigned char c : value) {
        if (c == '\\' || c == '"') { result += '\\'; result += static_cast<char>(c); }
        else if (c == '\n') result += "\\n";
        else if (c == '\r') result += "\\r";
        else if (c == '\t') result += "\\t";
        else if (c < 32) {
            constexpr char hex[] = "0123456789abcdef";
            result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15];
        } else result += static_cast<char>(c);
    }
    return result + '"';
}
unsigned long long ticks(FILETIME t) {
    return (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
}
bool alive(HANDLE h) {
    auto result = ::WaitForSingleObject(h, 0);
    require(result == WAIT_TIMEOUT || result == WAIT_OBJECT_0, "process wait failed");
    return result == WAIT_TIMEOUT;
}
struct Process {
    DWORD pid{};
    unsigned long long created{};
    fs::path image;
    Handle handle;
};
std::vector<Process> census(const wchar_t* name, std::optional<DWORD> parent = {}) {
    Handle snapshot{::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    require(bool(snapshot), "process snapshot failed");
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    std::vector<Process> result;
    BOOL more = ::Process32FirstW(snapshot.value, &entry);
    while (more) {
        if (_wcsicmp(entry.szExeFile, name) == 0 &&
            (!parent || entry.th32ParentProcessID == *parent)) {
            Handle handle{::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                                         FALSE, entry.th32ProcessID)};
            if (!handle) {
                // A disappearing process is not a retained identity. Record failure,
                // rather than inferring that an unopenable PID is our service.
                throw std::runtime_error("cannot retain observed process: " + std::to_string(::GetLastError()));
            }
            FILETIME created{}, exited{}, kernel{}, user{};
            require(::GetProcessTimes(handle.value, &created, &exited, &kernel, &user) != FALSE,
                    "GetProcessTimes failed");
            std::wstring image(32768, L'\0');
            DWORD n = static_cast<DWORD>(image.size());
            require(::QueryFullProcessImageNameW(handle.value, 0, image.data(), &n) != FALSE,
                    "QueryFullProcessImageName failed");
            image.resize(n);
            result.push_back(Process{entry.th32ProcessID, ticks(created), fs::path{image}, std::move(handle)});
        }
        more = ::Process32NextW(snapshot.value, &entry);
    }
    require(::GetLastError() == ERROR_NO_MORE_FILES, "process enumeration incomplete");
    return result;
}
bool same(const Process& a, const Process& b) { return a.pid == b.pid && a.created == b.created; }
std::string identities(const std::vector<Process>& processes) {
    std::string result = "[";
    for (const auto& p : processes) {
        if (result.size() != 1) result += ',';
        result += "{\"pid\":" + std::to_string(p.pid) + ",\"created\":" + std::to_string(p.created)
            + ",\"image\":" + quoted(path_text(p.image)) + "}";
    }
    return result + ']';
}
void require_default_host() {
    wchar_t value[2]{};
    require(::GetEnvironmentVariableW(L"MQB_OWNERSHIP_DISPOSABLE_HOST", value, 2) == 1 && value[0] == L'1',
            "default endpoint experiment requires an explicitly disposable host");
    ::SetLastError(ERROR_SUCCESS);
    const auto size = ::GetEnvironmentVariableW(L"_MSPDBSRV_ENDPOINT_", value, 2);
    require(size == 0 && ::GetLastError() == ERROR_ENVVAR_NOT_FOUND,
            "default endpoint experiment rejects an ambient endpoint override");
}
std::vector<Process> new_servers(const std::vector<Process>& before, const fs::path& compiler) {
    auto now = census(L"mspdbsrv.exe");
    std::vector<Process> result;
    for (auto& process : now) {
        if (_wcsicmp(process.image.parent_path().c_str(), compiler.parent_path().c_str()) != 0) continue;
        if (std::none_of(before.begin(), before.end(), [&](const auto& p) { return same(p, process); }))
            result.push_back(std::move(process));
    }
    return result;
}

struct Owners { DWORD error{}; bool includes_server{}; std::string identities{"[]"}; };
Owners pdb_owners(const fs::path& pdb, const Process& server) {
    DWORD session{};
    wchar_t key[CCH_RM_SESSION_KEY + 1]{};
    DWORD error = ::RmStartSession(&session, 0, key);
    if (error != ERROR_SUCCESS) return {error, false, "[]"};
    struct End { DWORD session; ~End() { ::RmEndSession(session); } } end{session};
    const auto path = pdb.wstring();
    LPCWSTR resource = path.c_str();
    error = ::RmRegisterResources(session, 1, &resource, 0, nullptr, 0, nullptr);
    if (error != ERROR_SUCCESS) return {error, false, "[]"};
    std::vector<RM_PROCESS_INFO> processes(8);
    UINT count{}, needed{};
    DWORD reasons{};
    for (unsigned attempt = 0; attempt != 4; ++attempt) {
        count = static_cast<UINT>(processes.size());
        error = ::RmGetList(session, &needed, &count, processes.data(), &reasons);
        if (error != ERROR_MORE_DATA) break;
        if (needed > 4096) return {ERROR_MORE_DATA, false, "[]"};
        processes.resize(std::max<std::size_t>(needed, processes.size() * 2));
    }
    if (error != ERROR_SUCCESS) return {error, false, "[]"};
    Owners result;
    result.identities = "[";
    for (UINT i = 0; i < count; ++i) {
        const auto& p = processes[i].Process;
        if (i) result.identities += ',';
        result.identities += "{\"pid\":" + std::to_string(p.dwProcessId)
            + ",\"created\":" + std::to_string(ticks(p.ProcessStartTime)) + "}";
        result.includes_server |= p.dwProcessId == server.pid && ticks(p.ProcessStartTime) == server.created;
    }
    result.identities += ']';
    return result;
}

void save_result(const fs::path& dir, const std::string& label, const RunResult& result) {
    if (result) {
        write(dir / (label + ".stdout.txt"), result->stdout_text);
        write(dir / (label + ".stderr.txt"), result->stderr_text);
        write(dir / (label + ".result.json"), "{\"exit_code\":" + std::to_string(result->exit_code)
              + ",\"cancelled\":" + (result->termination == mqb::process::ProcessTermination::cancelled ? "true" : "false") + "}\n");
    } else {
        write(dir / (label + ".error.txt"), result.error().message + " native=" + std::to_string(result.error().native_code));
        write(dir / (label + ".result.json"), "{\"infrastructure_error\":true,\"native_code\":"
              + std::to_string(result.error().native_code) + "}\n");
    }
}
RunResult invoke(const MsvcToolchain& toolchain, const fs::path& executable,
                 const fs::path& dir, const std::string& label, std::vector<std::string> arguments) {
    ProcessSpec spec;
    spec.executable = executable;
    spec.arguments = std::move(arguments);
    spec.working_directory = dir;
    spec.environment = toolchain.environment;
    std::string command = path_text(executable) + '\n';
    for (const auto& arg : spec.arguments) command += arg + '\n';
    write(dir / (label + ".argv.txt"), command);
    WindowsProcessRunner runner;
    auto result = runner.run(spec); // Deliberately unmanaged real compiler / linker.
    save_result(dir, label, result);
    return result;
}
int exit_code(const RunResult& r) { return r ? r->exit_code : -1; }
void success(const RunResult& r, const std::string& label) {
    require(r.has_value(), label + ": process infrastructure error");
    require(r->exit_code == 0, label + ": compiler control failed (see complete diagnostics)");
}
bool pch(const std::string& profile) { return profile.starts_with("pch-"); }
bool modules(const std::string& profile) { return profile.starts_with("modules-"); }
std::vector<std::string> flags(const fs::path& dir, const std::string& profile) {
    return {"/nologo", "/c", "/std:c++latest", "/EHsc", "/FS",
            profile.starts_with("ZI-") ? "/ZI" : "/Zi",
            profile.ends_with("release") ? "/O2" : "/Od",
            profile.ends_with("release") ? "/MT" : "/MTd",
            "/Fd" + path_text(dir / "compiler.pdb")};
}
std::string prefix(const std::string& profile) {
    if (pch(profile)) return "#include \"common.hpp\"\n";
    if (modules(profile)) return "import ownership;\n";
    return "";
}
RunResult compile(const MsvcToolchain& tc, const fs::path& dir, const std::string& profile,
                  const std::string& stem, bool large_object = false) {
    auto args = flags(dir, profile);
    // The fixed 24k-function /ZI drain fixture exceeds ordinary COFF sections.
    // Keep its input/debug/PDB behavior; widen only its object section indices.
    if (large_object) args.push_back("/bigobj");
    if (pch(profile)) {
        args.push_back("/Yucommon.hpp"); args.push_back("/Fp" + path_text(dir / "common.pch"));
    }
    if (modules(profile)) {
        args.push_back("/reference"); args.push_back("ownership=" + path_text(dir / "ownership.ifc"));
    }
    args.push_back("/Fo" + path_text(dir / (stem + ".obj")));
    args.push_back(path_text(dir / (stem + ".cpp")));
    return invoke(tc, tc.identity.compiler, dir, stem, std::move(args));
}
void prepare(const MsvcToolchain& tc, const fs::path& dir, const std::string& profile) {
    fs::create_directories(dir);
    if (pch(profile)) {
        write(dir / "common.hpp", "#pragma once\nstruct Common { int value; };\n");
        write(dir / "prefix.cpp", "#include \"common.hpp\"\n");
        auto args = flags(dir, profile);
        args.insert(args.end(), {"/Yccommon.hpp", "/Fp" + path_text(dir / "common.pch"),
                                "/Fo" + path_text(dir / "prefix.obj"), path_text(dir / "prefix.cpp")});
        success(invoke(tc, tc.identity.compiler, dir, "prefix", std::move(args)), "PCH creation");
    }
    if (modules(profile)) {
        write(dir / "ownership.ixx", "export module ownership;\nexport int answer() { return 42; }\n");
        auto args = flags(dir, profile);
        args.insert(args.end(), {"/interface", "/TP", "/ifcOutput", path_text(dir / "ownership.ifc"),
                                "/Fo" + path_text(dir / "provider.obj"), path_text(dir / "ownership.ixx")});
        success(invoke(tc, tc.identity.compiler, dir, "provider", std::move(args)), "module creation");
    }
    write(dir / "warm.cpp", prefix(profile) + "int warm() { return 42; }\n");
    success(compile(tc, dir, profile, "warm"), "warm compiler control");
    require(fs::exists(dir / "compiler.pdb"), "control must create real compiler PDB");
}
void workload(const fs::path& dir, const std::string& profile, unsigned functions = 6000) {
    // Fixed input, not repeatedly enlarged until overlap or a favorable result.
    for (unsigned worker = 0; worker != 2; ++worker) {
        std::string source = prefix(profile);
        for (unsigned i = 0; i != functions; ++i) {
            auto id = std::to_string(worker) + "_" + std::to_string(i);
            source += "struct T" + id + " { int a; int b; };\n__declspec(noinline) int f" + id
                + "(int x) { T" + id + " t{x, x+1}; return t.a+t.b; }\n";
        }
        if (worker == 0) source += "extern int other(); int main() { return other() == 42 ? 0 : 1; }\n";
        else source += modules(profile) ? "int other() { return answer(); }\n" : "int other() { return 42; }\n";
        write(dir / ("work" + std::to_string(worker) + ".cpp"), source);
    }
}

int root(int argc, wchar_t** argv, bool drain) {
    require(argc == (drain ? 8 : 7), "root arguments");
    MsvcToolchain tc;
    tc.identity.compiler = argv[2]; // Environment already supplied by the measured parent.
    const fs::path dir{argv[3]};
    const auto profile = utf8(argv[4]);
    Handle ready{::OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[5])};
    Handle release{::OpenEventW(SYNCHRONIZE, FALSE, argv[6])};
    Handle cancel{drain ? ::OpenEventW(SYNCHRONIZE, FALSE, argv[7]) : nullptr};
    require(bool(ready) && bool(release) && (!drain || bool(cancel)), "root event open failed");
    prepare(tc, dir, profile);
    // The drain fixture has one active and one pending TU. Fixed input, never
    // enlarged/retried to obtain overlap. Neither compiler receives a kill token.
    if (drain) workload(dir, profile, 24000);
    write(dir / "root.pid", std::to_string(::GetCurrentProcessId()));
    require(::SetEvent(ready.value) != FALSE, "root ready failed");
    // Fixture watchdog only; not a product cancellation deadline.
    require(::WaitForSingleObject(release.value, 120000) == WAIT_OBJECT_0, "root fixture watchdog");
    if (!drain) return 0;
    const auto first = compile(tc, dir, profile, "work0", true);
    const auto status = ::WaitForSingleObject(cancel.value, 0);
    require(status == WAIT_TIMEOUT || status == WAIT_OBJECT_0, "admission cancellation wait failed");
    const bool stopped = status == WAIT_OBJECT_0;
    int pending_exit = -2;
    if (!stopped) pending_exit = exit_code(compile(tc, dir, profile, "work1", true));
    write(dir / "drain.json", "{\"stop_observed\":" + std::string{stopped ? "true" : "false"}
          + ",\"work_compiles_dispatched\":" + (stopped ? "1" : "2")
          + ",\"first_compile_exit\":" + std::to_string(exit_code(first))
          + ",\"pending_compile_exit\":" + std::to_string(pending_exit)
          + ",\"safe_to_transfer_write_lease\":false}\n");
    // Missing the active boundary is a failed fixture, not evidence of safe cancellation.
    return stopped && exit_code(first) == 0 ? 0 : 1;
}

int measure(int argc, wchar_t** argv, bool default_endpoint) {
    require(argc == 7, "measure arguments");
    if (default_endpoint) require_default_host();
    const fs::path dir = fs::absolute(argv[2]);
    const std::string profile = utf8(argv[3]), origin = utf8(argv[4]), ending = utf8(argv[5]);
    const std::wstring fixture_id = argv[6];
    const bool drain = ending == "drain";
    require(profile == "zi-debug" || profile == "ZI-debug" || profile == "zi-release"
            || profile == "pch-debug" || profile == "pch-release"
            || profile == "modules-debug" || profile == "modules-release", "unknown profile");
    require(origin == "preexisting" || origin == "A-started", "unknown origin");
    require(ending == "cancel" || ending == "normal" || ending == "unmanaged-normal"
            || (default_endpoint && drain), "unknown ending");
    fs::create_directories(dir);
    WindowsProcessRunner runner;
    mqb::msvc::DiscoveryOptions options;
    options.preference = mqb::msvc::ToolchainPreference::visual_studio;
    options.cache_file = fs::path{}; // No shared discovery-cache writes in the experiment.
    auto discovered = mqb::msvc::MsvcToolchainLocator{runner}.discover(options);
    if (!discovered) {
        write(dir / "discovery.error.txt", discovered.error().message);
        throw std::runtime_error("MSVC discovery failed; see discovery.error.txt");
    }
    auto tc = std::move(*discovered);
    // Private endpoints are still fixture isolation only. The default mode
    // removes the override; fixture_id then names events, never the PDB endpoint.
    for (const char* name : {"_MSPDBSRV_ENDPOINT_", "CL", "_CL_", "LINK", "_LINK_"}) {
        std::erase_if(tc.environment, [&](const auto& e) { return _stricmp(e.name.c_str(), name) == 0; });
        const bool private_endpoint = !default_endpoint && name == std::string{"_MSPDBSRV_ENDPOINT_"};
        tc.environment.push_back({name, private_endpoint ? utf8(fixture_id) : "", !private_endpoint});
    }
    write(dir / "toolchain.txt", path_text(tc.identity.compiler) + '\n' + tc.identity.version + '\n'
          + tc.identity.binary_stamp + "\nendpoint=" + (default_endpoint ? "<unset/default>" : utf8(fixture_id)) + '\n');
    auto before = census(L"mspdbsrv.exe");
    write(dir / "baseline-servers.json", identities(before) + '\n');
    require(!default_endpoint || before.empty(), "default endpoint is not pristine after discovery");
    if (origin == "preexisting") prepare(tc, dir / "seed", profile);
    const auto ready_name = L"Local\\MQB-ownership-ready-" + fixture_id;
    const auto release_name = L"Local\\MQB-ownership-release-" + fixture_id;
    const auto cancel_name = L"Local\\MQB-ownership-cancel-" + fixture_id;
    Handle ready{::CreateEventW(nullptr, TRUE, FALSE, ready_name.c_str())};
    Handle release{::CreateEventW(nullptr, TRUE, FALSE, release_name.c_str())};
    Handle admission_cancel{drain ? ::CreateEventW(nullptr, TRUE, FALSE, cancel_name.c_str()) : nullptr};
    require(bool(ready) && bool(release) && (!drain || bool(admission_cancel)), "fixture events failed");
    std::stop_source stop;
    ProcessSpec a;
    a.executable = self();
    a.arguments = {drain ? "--drain-root" : "--root", path_text(tc.identity.compiler), path_text(dir / "A"), profile,
                   utf8(ready_name), utf8(release_name)};
    if (drain) a.arguments.push_back(utf8(cancel_name));
    a.environment = tc.environment;
    if (ending == "normal" || ending == "cancel") a.cancellation = stop.get_token();
    auto a_future = std::async(std::launch::async, [&] {
        auto result = runner.run(a);
        save_result(dir, "A", result); // Also preserve early-root failures before readiness.
        return result;
    });
    struct Release {
        Handle& event; Handle& cancel; std::stop_source& stop; std::future<RunResult>& future;
        ~Release() {
            stop.request_stop();
            if (cancel) ::SetEvent(cancel.value);
            ::SetEvent(event.value);
            if (future.valid()) future.wait();
        }
    } release_on_error{release, admission_cancel, stop, a_future};
    bool is_ready = false;
    for (unsigned attempt = 0; attempt != 600; ++attempt) {
        const auto status = ::WaitForSingleObject(ready.value, 100);
        require(status == WAIT_TIMEOUT || status == WAIT_OBJECT_0, "A readiness wait failed");
        if (status == WAIT_OBJECT_0) { is_ready = true; break; }
        if (a_future.wait_for(0ms) == std::future_status::ready) break;
    }
    require(is_ready, "A did not reach compiled-and-ready boundary; see A/ and A.result.json");
    auto servers = new_servers(before, tc.identity.compiler);
    require(servers.size() == 1 && alive(servers[0].handle.value), "cannot attribute exactly one new live MSPDBSRV");
    const auto& server = servers[0];
    prepare(tc, dir / "B", profile);
    auto after_b = new_servers(before, tc.identity.compiler);
    require(after_b.size() == 1 && same(server, after_b[0]), "B did not retain the same observed endpoint service");
    auto warm_owners = pdb_owners(dir / "B/compiler.pdb", server);
    workload(dir / "B", profile);
    std::vector<Process> active_a;
    if (drain) {
        DWORD root_pid{};
        std::ifstream pid_file(dir / "A/root.pid");
        require(bool(pid_file >> root_pid) && root_pid != 0, "A root identity unavailable");
        require(::SetEvent(release.value) != FALSE, "start A drain workload failed");
        const auto deadline = std::chrono::steady_clock::now() + 15s;
        do {
            // PID only filters observations; it never authorizes termination.
            active_a = census(L"cl.exe", root_pid);
            if (!active_a.empty() || a_future.wait_for(0ms) == std::future_status::ready) break;
            std::this_thread::sleep_for(10ms);
        } while (std::chrono::steady_clock::now() < deadline);
        for (const auto& p : active_a)
            require(_wcsicmp(p.image.c_str(), tc.identity.compiler.c_str()) == 0, "A compiler image mismatch");
    }
    auto b0 = std::async(std::launch::async, [&] { return compile(tc, dir / "B", profile, "work0"); });
    auto b1 = std::async(std::launch::async, [&] { return compile(tc, dir / "B", profile, "work1"); });
    std::vector<Process> active;
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    do {
        active = census(L"cl.exe", ::GetCurrentProcessId());
        if (!active.empty()) break;
        if (b0.wait_for(0ms) == std::future_status::ready && b1.wait_for(0ms) == std::future_status::ready) break;
        std::this_thread::sleep_for(10ms);
    } while (std::chrono::steady_clock::now() < deadline);
    for (const auto& p : active)
        require(_wcsicmp(p.image.c_str(), tc.identity.compiler.c_str()) == 0, "B compiler image mismatch");
    const auto active_owners = pdb_owners(dir / "B/compiler.pdb", server);
    const auto a_owners_at_request = drain ? pdb_owners(dir / "A/compiler.pdb", server) : Owners{};
    const bool a_overlap = std::any_of(active_a.begin(), active_a.end(), [](const auto& p) { return alive(p.handle.value); });
    const bool overlap = std::any_of(active.begin(), active.end(), [](const auto& p) { return alive(p.handle.value); });
    // These are retained-handle observations, not proof of an in-flight PDB RPC.
    const auto requested = std::chrono::steady_clock::now();
    if (ending == "cancel") stop.request_stop();
    else if (drain) require(::SetEvent(admission_cancel.value) != FALSE, "stop A admission failed");
    else require(::SetEvent(release.value) != FALSE, "release A failed");
    auto a_result = a_future.get();
    const auto settled = std::chrono::steady_clock::now();
    const bool service_survived = alive(server.handle.value);
    // The surviving service can still own PDB resources after the A client exits.
    // An empty snapshot is not a durable no-writer certificate either.
    const auto a_owners_after = default_endpoint ? pdb_owners(dir / "A/compiler.pdb", server) : Owners{};
    const bool a_handles_signaled = std::all_of(active_a.begin(), active_a.end(), [](const auto& p) { return !alive(p.handle.value); });
    const bool pending_dispatched = fs::exists(dir / "A/work1.argv.txt");
    auto b0_result = b0.get(), b1_result = b1.get();
    int linked = -2, executed = -2;
    if (exit_code(b0_result) == 0 && exit_code(b1_result) == 0) {
        std::vector<std::string> args{"/NOLOGO", "/DEBUG", "/INCREMENTAL:NO", "/OUT:" + path_text(dir / "B/result.exe"),
                                      "/PDB:" + path_text(dir / "B/linked.pdb"), path_text(dir / "B/work0.obj"),
                                      path_text(dir / "B/work1.obj")};
        if (modules(profile)) args.push_back(path_text(dir / "B/provider.obj"));
        if (pch(profile)) args.push_back(path_text(dir / "B/prefix.obj"));
        auto result = invoke(tc, tc.linker, dir / "B", "link", std::move(args));
        linked = exit_code(result);
        if (linked == 0) executed = exit_code(invoke(tc, dir / "B/result.exe", dir / "B", "run", {}));
    }
    // Explicit separate recovery attempt, never substitutes for the first B result.
    write(dir / "B/recovery.cpp", prefix(profile) + "int recovery() { return 43; }\n");
    const auto recovery = compile(tc, dir / "B", profile, "recovery");
    const bool lifecycle_ok = a_result && (ending == "cancel"
        ? a_result->termination == mqb::process::ProcessTermination::cancelled && a_result->exit_code != 0
        : a_result->termination == mqb::process::ProcessTermination::exited && a_result->exit_code == 0);
    const bool b_ok = exit_code(b0_result) == 0 && exit_code(b1_result) == 0 && linked == 0 && executed == 0;
    const bool drain_ok = !drain || (service_survived && b_ok && a_overlap && overlap
        && a_handles_signaled && !pending_dispatched && fs::exists(dir / "A/work0.obj"));
    const bool control_ok = ending != "unmanaged-normal" || (service_survived && b_ok);
    std::string report = "{\n\"schema\":2,\"profile\":" + quoted(profile) + ",\"origin\":" + quoted(origin)
        + ",\"ending\":" + quoted(ending) + ",\"server_pid\":" + std::to_string(server.pid)
        + ",\"server_created\":" + std::to_string(server.created)
        + ",\"server_image\":" + quoted(path_text(server.image))
        + ",\"server_survived_A\":" + (service_survived ? "true" : "false")
        + ",\"B_compiler_overlap_observed\":" + (overlap ? "true" : "false")
        + ",\"B_observed_compilers\":" + identities(active)
        + ",\"endpoint_mode\":" + quoted(default_endpoint ? "default" : "private")
        + ",\"request_to_A_result_ms\":" + std::to_string(std::chrono::duration<double, std::milli>(settled - requested).count())
        + ",\"drain_requested\":" + (drain ? "true" : "false")
        + ",\"A_observed_compilers\":" + identities(active_a)
        + ",\"A_compiler_overlap_observed\":" + (a_overlap ? "true" : "false")
        + ",\"A_observed_compilers_signaled\":" + (drain ? (a_handles_signaled ? "true" : "false") : "null")
        + ",\"A_pending_compile_dispatched\":" + (drain ? (pending_dispatched ? "true" : "false") : "null")
        + ",\"A_pdb_owner_at_request_error\":" + (drain ? std::to_string(a_owners_at_request.error) : "null")
        + ",\"A_pdb_owners_at_request\":" + a_owners_at_request.identities
        + ",\"A_pdb_owner_after_A_error\":" + (default_endpoint ? std::to_string(a_owners_after.error) : "null")
        + ",\"A_pdb_owners_after_A\":" + a_owners_after.identities
        + ",\"A_pdb_service_owner_after_A\":" + (default_endpoint ? (a_owners_after.includes_server ? "true" : "false") : "null")
        + ",\"drain_control_ok\":" + (drain_ok ? "true" : "false")
        + ",\"warm_pdb_owner_error\":" + std::to_string(warm_owners.error)
        + ",\"warm_pdb_owners\":" + warm_owners.identities
        + ",\"active_pdb_owner_error\":" + std::to_string(active_owners.error)
        + ",\"active_pdb_owners\":" + active_owners.identities
        + ",\"B_pdb_service_identity_observed\":" + ((warm_owners.includes_server || active_owners.includes_server) ? "true" : "false")
        + ",\"A_exit\":" + std::to_string(exit_code(a_result))
        + ",\"B0_exit\":" + std::to_string(exit_code(b0_result)) + ",\"B1_exit\":" + std::to_string(exit_code(b1_result))
        + ",\"B_link_exit\":" + std::to_string(linked) + ",\"B_run_exit\":" + std::to_string(executed)
        + ",\"recovery_compile_exit\":" + std::to_string(exit_code(recovery))
        + ",\"lifecycle_ok\":" + (lifecycle_ok ? "true" : "false")
        + ",\"unmanaged_control_ok\":" + (control_ok ? "true" : "false")
        + ",\"safe_to_integrate_cancellation\":false,\"safe_to_transfer_write_lease\":false\n}\n";
    write(dir / "observation.json", report);
    std::cout << report;
    return lifecycle_ok && control_ok && drain_ok ? 0 : 1;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        require(argc >= 2, "use --case/--default-case/--measure/--root");
        const std::wstring mode = argv[1];
        if (mode == L"--root" || mode == L"--drain-root") return root(argc, argv, mode == L"--drain-root");
        if (mode == L"--measure" || mode == L"--measure-default") return measure(argc, argv, mode == L"--measure-default");
        const bool default_endpoint = mode == L"--default-case";
        require((mode == L"--case" || default_endpoint) && argc == 7, "case arguments");
        const fs::path dir = fs::absolute(argv[2]);
        fs::create_directories(dir);
        if (default_endpoint) {
            require_default_host();
            const auto before = census(L"mspdbsrv.exe");
            write(dir / "default-preflight.json", identities(before) + '\n');
            // Never terminate a pre-existing host service to manufacture a clean test.
            require(before.empty(), "default endpoint host is not clean; experiment refused");
        }
        // A fixture-wide outer Job bounds BOTH projects. For default endpoints
        // it is authorized only on a disposable clean host, never a developer PC.
        // No global service termination, PID-authorized kill, or product changes.
        ProcessSpec spec;
        spec.executable = self();
        spec.arguments = {default_endpoint ? "--measure-default" : "--measure"};
        for (int i = 2; i < argc; ++i) spec.arguments.push_back(utf8(argv[i]));
        std::stop_source lifetime;
        spec.cancellation = lifetime.get_token();
        WindowsProcessRunner runner;
        auto result = runner.run(spec);
        if (default_endpoint) {
            const auto remaining = census(L"mspdbsrv.exe");
            const bool clean = result.has_value() && remaining.empty();
            write(dir / "default-envelope.json", "{\"endpoint_override_absent\":true,\"remaining_servers\":"
                  + identities(remaining) + ",\"outer_lifecycle_verified\":" + (result ? "true" : "false")
                  + ",\"cleanup_verified\":" + (clean ? "true" : "false") + "}\n");
            if (!clean) {
                if (result) { std::cout << result->stdout_text; std::cerr << result->stderr_text; }
                else std::cerr << result.error().message << '\n';
                std::cerr << "DEFAULT_ENDPOINT_CLEANUP_UNPROVEN: do not run another case\n";
                return 1;
            }
        }
        if (!result) { std::cerr << "OUTER_CLEANUP_ERROR " << result.error().message << '\n'; return 1; }
        std::cout << result->stdout_text;
        std::cerr << result->stderr_text;
        return result->exit_code;
    } catch (const std::exception& e) {
        std::cerr << "PROBE_INFRASTRUCTURE_FAILURE " << e.what() << '\n';
        return 1;
    }
}
