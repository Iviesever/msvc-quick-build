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
                  const std::string& stem) {
    auto args = flags(dir, profile);
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
void workload(const fs::path& dir, const std::string& profile) {
    // Fixed input, not repeatedly enlarged until overlap or a favorable result.
    for (unsigned worker = 0; worker != 2; ++worker) {
        std::string source = prefix(profile);
        for (unsigned i = 0; i != 6000; ++i) {
            auto id = std::to_string(worker) + "_" + std::to_string(i);
            source += "struct T" + id + " { int a; int b; };\n__declspec(noinline) int f" + id
                + "(int x) { T" + id + " t{x, x+1}; return t.a+t.b; }\n";
        }
        if (worker == 0) source += "extern int other(); int main() { return other() == 42 ? 0 : 1; }\n";
        else source += modules(profile) ? "int other() { return answer(); }\n" : "int other() { return 42; }\n";
        write(dir / ("work" + std::to_string(worker) + ".cpp"), source);
    }
}

int root(int argc, wchar_t** argv) {
    require(argc == 7, "root arguments");
    MsvcToolchain tc;
    tc.identity.compiler = argv[2]; // Environment already supplied by the measured parent.
    const fs::path dir{argv[3]};
    Handle ready{::OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[5])};
    Handle release{::OpenEventW(SYNCHRONIZE, FALSE, argv[6])};
    require(bool(ready) && bool(release), "root event open failed");
    prepare(tc, dir, utf8(argv[4]));
    write(dir / "root.pid", std::to_string(::GetCurrentProcessId()));
    require(::SetEvent(ready.value) != FALSE, "root ready failed");
    // Fixture watchdog only; not a product cancellation deadline.
    require(::WaitForSingleObject(release.value, 120000) == WAIT_OBJECT_0, "root fixture watchdog");
    return 0;
}

int measure(int argc, wchar_t** argv) {
    require(argc == 7, "measure arguments");
    const fs::path dir = fs::absolute(argv[2]);
    const std::string profile = utf8(argv[3]), origin = utf8(argv[4]), ending = utf8(argv[5]);
    const std::wstring endpoint = argv[6];
    require(profile == "zi-debug" || profile == "ZI-debug" || profile == "zi-release"
            || profile == "pch-debug" || profile == "pch-release"
            || profile == "modules-debug" || profile == "modules-release", "unknown profile");
    require(origin == "preexisting" || origin == "A-started", "unknown origin");
    require(ending == "cancel" || ending == "normal" || ending == "unmanaged-normal", "unknown ending");
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
    // Test isolation only, not a supported product-service ownership policy.
    for (const char* name : {"_MSPDBSRV_ENDPOINT_", "CL", "_CL_", "LINK", "_LINK_"}) {
        std::erase_if(tc.environment, [&](const auto& e) { return _stricmp(e.name.c_str(), name) == 0; });
        tc.environment.push_back({name, name == std::string{"_MSPDBSRV_ENDPOINT_"} ? utf8(endpoint) : "",
                                  name != std::string{"_MSPDBSRV_ENDPOINT_"}});
    }
    write(dir / "toolchain.txt", path_text(tc.identity.compiler) + '\n' + tc.identity.version + '\n'
          + tc.identity.binary_stamp + "\nendpoint=" + utf8(endpoint) + '\n');
    auto before = census(L"mspdbsrv.exe");
    if (origin == "preexisting") prepare(tc, dir / "seed", profile);
    const auto ready_name = L"Local\\MQB-ownership-ready-" + endpoint;
    const auto release_name = L"Local\\MQB-ownership-release-" + endpoint;
    Handle ready{::CreateEventW(nullptr, TRUE, FALSE, ready_name.c_str())};
    Handle release{::CreateEventW(nullptr, TRUE, FALSE, release_name.c_str())};
    require(bool(ready) && bool(release), "fixture events failed");
    std::stop_source stop;
    ProcessSpec a;
    a.executable = self();
    a.arguments = {"--root", path_text(tc.identity.compiler), path_text(dir / "A"), profile,
                   utf8(ready_name), utf8(release_name)};
    a.environment = tc.environment;
    if (ending != "unmanaged-normal") a.cancellation = stop.get_token();
    auto a_future = std::async(std::launch::async, [&] {
        auto result = runner.run(a);
        save_result(dir, "A", result); // Also preserve early-root failures before readiness.
        return result;
    });
    struct Release {
        Handle& event; std::stop_source& stop; std::future<RunResult>& future;
        ~Release() { stop.request_stop(); ::SetEvent(event.value); if (future.valid()) future.wait(); }
    } release_on_error{release, stop, a_future};
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
    std::string observed_compilers = "[";
    for (const auto& process : active) {
        require(_wcsicmp(process.image.c_str(), tc.identity.compiler.c_str()) == 0,
                "observed child compiler image differs from selected toolchain");
        if (observed_compilers.size() != 1) observed_compilers += ',';
        observed_compilers += "{\"pid\":" + std::to_string(process.pid)
            + ",\"created\":" + std::to_string(process.created)
            + ",\"image\":" + quoted(path_text(process.image)) + "}";
    }
    observed_compilers += ']';
    const auto active_owners = pdb_owners(dir / "B/compiler.pdb", server);
    bool overlap = std::any_of(active.begin(), active.end(), [](const auto& p) { return alive(p.handle.value); });
    // No claim that a live cl.exe proves an RPC is in flight at this instant.
    if (ending == "cancel") stop.request_stop();
    else require(::SetEvent(release.value) != FALSE, "release A failed");
    auto a_result = a_future.get();
    const bool service_survived = alive(server.handle.value); // Immediate retained-handle observation.
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
    const bool control_ok = ending != "unmanaged-normal" || (service_survived && b_ok);
    std::string report = "{\n\"schema\":1,\"profile\":" + quoted(profile) + ",\"origin\":" + quoted(origin)
        + ",\"ending\":" + quoted(ending) + ",\"server_pid\":" + std::to_string(server.pid)
        + ",\"server_created\":" + std::to_string(server.created)
        + ",\"server_image\":" + quoted(path_text(server.image))
        + ",\"server_survived_A\":" + (service_survived ? "true" : "false")
        + ",\"B_compiler_overlap_observed\":" + (overlap ? "true" : "false")
        + ",\"B_observed_compilers\":" + observed_compilers
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
    return lifecycle_ok && control_ok ? 0 : 1;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        require(argc >= 2, "use --case/--measure/--root");
        const std::wstring mode = argv[1];
        if (mode == L"--root") return root(argc, argv);
        if (mode == L"--measure") return measure(argc, argv);
        require(mode == L"--case" && argc == 7, "case arguments");
        // A fixture-wide outer Job bounds *both* projects and their dedicated
        // endpoint. The inner A Job is the policy under test. No global service
        // termination, PID-authorized kill, or product environment changes.
        ProcessSpec spec;
        spec.executable = self();
        spec.arguments = {"--measure"};
        for (int i = 2; i < argc; ++i) spec.arguments.push_back(utf8(argv[i]));
        std::stop_source lifetime;
        spec.cancellation = lifetime.get_token();
        WindowsProcessRunner runner;
        auto result = runner.run(spec);
        if (!result) { std::cerr << "OUTER_CLEANUP_ERROR " << result.error().message << '\n'; return 1; }
        std::cout << result->stdout_text;
        std::cerr << result->stderr_text;
        return result->exit_code;
    } catch (const std::exception& e) {
        std::cerr << "PROBE_INFRASTRUCTURE_FAILURE " << e.what() << '\n';
        return 1;
    }
}
