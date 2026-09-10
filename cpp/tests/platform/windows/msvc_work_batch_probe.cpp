// Real-tool fixture for the opt-in typed batch, not a production CLI path.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <semaphore>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "mqb/msvc/MsvcCompiler.hpp"
#include "mqb/orchestration/BoundedWorkBatch.hpp"
#include "mqb/platform/windows/CommandLine.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"

namespace {
namespace fs = std::filesystem;
using namespace std::chrono_literals;
using mqb::platform::windows::WindowsProcessRunner;
using CompileResult = std::expected<mqb::process::ProcessResult, mqb::msvc::CompilerError>;
using Batch = mqb::orchestration::WorkBatchReport<CompileResult>;
void require(bool value, const std::string& text) { if (!value) throw std::runtime_error(text); }
std::string text(const fs::path& path) {
    auto converted = mqb::platform::windows::utf16_to_utf8(path.wstring());
    require(converted.has_value(), "invalid path encoding"); return *converted;
}
std::string json_string(const std::string& value) {
    std::string output = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') { output += '\\'; output += static_cast<char>(c); }
        else if (c < 32) { output += "\\u00"; output += hex[c >> 4]; output += hex[c & 15]; }
        else output += static_cast<char>(c);
    }
    return output + '"';
}
void write(const fs::path& path, const std::string& contents) {
    std::ofstream file(path, std::ios::binary); file << contents; file.flush();
    require(bool(file), "cannot preserve " + text(path));
}
struct Handle {
    HANDLE value{};
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) ::CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& h) noexcept : value(std::exchange(h.value, nullptr)) {}
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};
std::uint64_t ticks(FILETIME t) { return (std::uint64_t{t.dwHighDateTime} << 32) | t.dwLowDateTime; }
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
            require(_wcsicmp(image.c_str(), expected_image.c_str()) == 0, "unexpected compiler image");
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
struct CompilerRunner final : mqb::process::ProcessRunner {
    WindowsProcessRunner native;
    std::atomic<unsigned> invocations{0};
    std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>
    run(const mqb::process::ProcessSpec& spec) override {
        require(!spec.cancellation.stop_possible(), "compiler received a forbidden terminating token");
        invocations.fetch_add(1);
        return native.run(spec);
    }
};
std::string state(mqb::orchestration::WorkBatchOutcome value) {
    switch (value) {
    case mqb::orchestration::WorkBatchOutcome::succeeded: return "succeeded";
    case mqb::orchestration::WorkBatchOutcome::failed: return "failed";
    case mqb::orchestration::WorkBatchOutcome::cancelled: return "cancelled";
    }
    throw std::runtime_error("unknown typed batch result");
}
void save_process(const fs::path& dir, const std::string& stem, const mqb::process::ProcessResult& result) {
    write(dir / (stem + ".stdout.txt"), result.stdout_text);
    write(dir / (stem + ".stderr.txt"), result.stderr_text);
    write(dir / (stem + ".result.json"), "{\"exit_code\":" + std::to_string(result.exit_code)
        + ",\"cancelled\":" + (result.termination == mqb::process::ProcessTermination::cancelled ? "true" : "false") + "}\n");
}
std::string save_attempts(const fs::path& dir, const Batch& report) {
    std::string rows = "[";
    for (std::size_t i = 0; i < report.attempts.size(); ++i) {
        if (i) rows += ',';
        const auto stem = "work" + std::to_string(i);
        const auto* result = std::get_if<CompileResult>(&report.attempts[i]);
        rows += "{\"index\":" + std::to_string(i) + ",\"state\":";
        if (!result) {
            if (std::holds_alternative<std::monostate>(report.attempts[i])) rows += "\"not_started\"}";
            else {
                rows += "\"exception\"}";
                const auto& exception = std::get<std::exception_ptr>(report.attempts[i]);
                try { if (exception) std::rethrow_exception(exception); }
                catch (const std::exception& error) { write(dir / (stem + ".exception.txt"), error.what()); }
                catch (...) { write(dir / (stem + ".exception.txt"), "non-standard exception"); }
            }
            continue;
        }
        if (*result) {
            save_process(dir, stem, **result);
            rows += "\"succeeded\",\"exit_code\":" + std::to_string((*result)->exit_code) + "}";
        } else {
            const auto& error = result->error();
            write(dir / (stem + ".compiler-error.txt"), error.message);
            rows += "\"failed\",\"compiler_error_code\":" + std::to_string(static_cast<int>(error.code));
            if (error.process_result) {
                save_process(dir, stem, *error.process_result);
                rows += ",\"exit_code\":" + std::to_string(error.process_result->exit_code);
            } else {
                rows += ",\"exit_code\":null";
                if (error.process_error) write(dir / (stem + ".process-error.txt"), error.process_error->message
                    + " native=" + std::to_string(error.process_error->native_code));
            }
            rows += '}';
        }
    }
    return rows + ']';
}
int run(int argc, wchar_t** argv) {
    require(argc == 4, "use OUTPUT CONFIGURATION MODE");
    wchar_t authorized[2]{};
    require(::GetEnvironmentVariableW(L"MQB_BATCH_DISPOSABLE_HOST", authorized, 2) == 1 && authorized[0] == L'1',
            "real batch fixture requires a disposable host");
    const fs::path root = fs::absolute(argv[1]);
    require(!fs::exists(root), "refusing to overwrite a previous case"); fs::create_directories(root);
    const std::wstring config{argv[2]}, mode{argv[3]};
    require(config == L"Debug" || config == L"Release", "unknown configuration");
    require(mode == L"complete" || mode == L"cancel" || mode == L"failure-cancel", "unknown mode");
    const bool cancel = mode != L"complete", fail = mode == L"failure-cancel";
    WindowsProcessRunner discovery_runner;
    mqb::msvc::DiscoveryOptions discovery;
    discovery.preference = mqb::msvc::ToolchainPreference::visual_studio;
    discovery.cache_file = fs::path{};
    auto found = mqb::msvc::MsvcToolchainLocator{discovery_runner}.discover(discovery);
    require(found.has_value(), found ? "" : found.error().message);
    auto toolchain = std::move(*found);
    for (const char* name : {"CL", "_CL_", "LINK", "_LINK_"}) {
        std::erase_if(toolchain.environment, [&](const auto& e) { return _stricmp(e.name.c_str(), name) == 0; });
        toolchain.environment.push_back({name, "", true});
    }
    write(root / "toolchain.txt", text(toolchain.identity.compiler) + '\n' + toolchain.identity.version + '\n'
          + toolchain.identity.binary_stamp + '\n');
    std::vector<mqb::msvc::MsvcCompileRecipe> recipes;
    for (unsigned i = 0; i != 3; ++i) {
        const auto stem = "work" + std::to_string(i);
        std::string source;
        if (i < 2) for (unsigned j = 0; j != 12000; ++j) {
            const auto id = std::to_string(i) + "_" + std::to_string(j);
            source += "struct T" + id + " { int a; int b; };\n__declspec(noinline) int f" + id
                + "(int n) { T" + id + " t{n,n+1}; return t.a+t.b; }\n";
        }
        if (i == 0) source += "int batch_helper(); int main(){return batch_helper()==42?0:1;}\n";
        if (i == 1) source += "int batch_helper(){return 42;}\n";
        if (i == 2) source += "int pending_work(){return 1;}\n";
        if (i == 1 && fail) source += "static_assert(false, \"MQB_BATCH_EXPECTED_COMPILER_FAILURE\");\n";
        write(root / (stem + ".cpp"), source);
        mqb::msvc::CompileInvocation invocation;
        invocation.source = root / (stem + ".cpp"); invocation.object = root / (stem + ".obj");
        invocation.working_directory = root;
        invocation.options.configuration = config == L"Debug" ? mqb::BuildConfiguration::debug : mqb::BuildConfiguration::release;
        invocation.options.runtime_library = config == L"Debug" ? mqb::RuntimeLibrary::mtd : mqb::RuntimeLibrary::mt;
        invocation.options.additional_arguments = {"/Zi", "/FS", "/bigobj", "/Fd" + text(root / "compiler.pdb")};
        auto recipe = mqb::msvc::MsvcCompiler::build_recipe(toolchain, invocation);
        require(recipe.has_value(), recipe ? "" : recipe.error().message);
        require(!recipe->process.cancellation.stop_possible(), "recipe introduced a process kill token");
        std::string args = text(recipe->process.executable) + '\n';
        for (const auto& arg : recipe->process.arguments) args += arg + '\n';
        write(root / (stem + ".planned-argv.txt"), args);
        recipes.push_back(std::move(*recipe));
    }
    CompilerRunner runner;
    const mqb::msvc::MsvcCompiler compiler{toolchain, runner};
    std::stop_source admission;
    std::counting_semaphore<2> entered{0}, launch{0};
    auto batch = std::async(std::launch::async, [&] {
        return mqb::orchestration::run_work_batch(3, 2, admission.get_token(), [&](std::size_t i) -> CompileResult {
            if (i < 2) { entered.release(); launch.acquire(); }
            // Actual adapter -> scheduler -> existing compiler -> existing runner.
            return compiler.execute_recipe(recipes[i]);
        });
    });
    const bool first = entered.try_acquire_for(10s), second = entered.try_acquire_for(10s);
    launch.release(2); // Also releases a failed fixture instead of deadlocking cleanup.
    std::vector<Child> active;
    std::string observation_error;
    bool overlap = false;
    try {
        const auto deadline = std::chrono::steady_clock::now() + 15s;
        do {
            active = compilers(toolchain.identity.compiler);
            if (active.size() == 2 && live(active[0]) && live(active[1])) { overlap = true; break; }
            if (batch.wait_for(2ms) == std::future_status::ready) break;
        } while (std::chrono::steady_clock::now() < deadline);
    } catch (const std::exception& error) { observation_error = error.what(); }
    if (cancel || !overlap || !first || !second || !observation_error.empty()) admission.request_stop();
    auto result = batch.get(); // Does not return until both admitted callbacks drain.
    const auto items = save_attempts(root, result);
    bool retained_exited = true;
    for (const auto& child : active) retained_exited = retained_exited && !live(child);
    const auto expected = fail ? mqb::orchestration::WorkBatchOutcome::failed
        : (cancel ? mqb::orchestration::WorkBatchOutcome::cancelled : mqb::orchestration::WorkBatchOutcome::succeeded);
    const bool clean_observation = first && second && overlap && observation_error.empty() && retained_exited;
    const bool schedule_ok = result.scheduling && *result.scheduling
        && (*result.scheduling)->started_count == (cancel ? 2U : 3U)
        && (*result.scheduling)->admission_stop_observed == cancel;
    bool originals_ok = result.attempts.size() == 3;
    if (originals_ok) {
        for (std::size_t i = 0; i < (cancel ? 2U : 3U); ++i) {
            const auto* original = std::get_if<CompileResult>(&result.attempts[i]);
            if (i == 1 && fail) {
                originals_ok = originals_ok && original && !*original
                    && original->error().code == mqb::msvc::CompilerErrorCode::compilation_failed
                    && original->error().process_result && original->error().process_result->exit_code != 0
                    && (original->error().process_result->stdout_text + original->error().process_result->stderr_text)
                        .find("MQB_BATCH_EXPECTED_COMPILER_FAILURE") != std::string::npos;
            } else originals_ok = originals_ok && original && original->has_value() && (*original)->exit_code == 0;
        }
        if (cancel) originals_ok = originals_ok && std::holds_alternative<std::monostate>(result.attempts[2]);
    }
    unsigned link_calls = 0, run_calls = 0, commits = 0;
    int link_exit = -2, run_exit = -2;
    // This is the caller's predecessor-result gate, NOT a product transaction.
    // No failure/stop path can link or write this fixture's commit sentinel.
    if (clean_observation && result.all_succeeded()) {
        mqb::process::ProcessSpec link;
        link.executable = toolchain.linker; link.working_directory = root; link.environment = toolchain.environment;
        link.arguments = {"/NOLOGO", "/DEBUG", "/INCREMENTAL:NO", "/OUT:" + text(root / "program.exe"),
                          "/PDB:" + text(root / "linked.pdb")};
        for (unsigned i = 0; i != 3; ++i) link.arguments.push_back(text(root / ("work" + std::to_string(i) + ".obj")));
        std::string args = text(link.executable) + '\n'; for (const auto& arg : link.arguments) args += arg + '\n';
        write(root / "link.argv.txt", args);
        ++link_calls;
        auto linked = discovery_runner.run(link);
        if (linked) { save_process(root, "link", *linked); link_exit = linked->exit_code; }
        else write(root / "link.error.txt", linked.error().message);
        if (link_exit == 0) {
            mqb::process::ProcessSpec program; program.executable = root / "program.exe"; program.working_directory = root;
            ++run_calls; auto ran = discovery_runner.run(program);
            if (ran) { save_process(root, "run", *ran); run_exit = ran->exit_code; }
            else write(root / "run.error.txt", ran.error().message);
            if (run_exit == 0) { write(root / "fixture-commit.json", "{\"fixture_only\":true}\n"); ++commits; }
        }
    }
    const bool downstream_ok = cancel ? link_calls == 0 && run_calls == 0 && commits == 0
        && !fs::exists(root / "program.exe") && !fs::exists(root / "fixture-commit.json")
        : link_calls == 1 && run_calls == 1 && commits == 1 && link_exit == 0 && run_exit == 0;
    std::string schedule = "null";
    if (result.scheduling) {
        if (*result.scheduling) {
            const auto& summary = **result.scheduling;
            schedule = "{\"worker_count\":" + std::to_string(summary.worker_count)
                + ",\"started_count\":" + std::to_string(summary.started_count)
                + ",\"stop_requested\":" + (summary.stop_requested ? "true" : "false")
                + ",\"admission_stop_observed\":" + (summary.admission_stop_observed ? "true" : "false")
                + ",\"stopped_before_all_items\":" + (summary.stopped_before_all_items ? "true" : "false") + "}";
        } else schedule = "{\"error_code\":" + std::to_string(static_cast<int>(result.scheduling->error().code))
            + ",\"message\":" + json_string(result.scheduling->error().message) + "}";
    }
    if (result.dispatch_exception) {
        try { std::rethrow_exception(result.dispatch_exception); }
        catch (const std::exception& error) { write(root / "dispatch-exception.txt", error.what()); }
        catch (...) { write(root / "dispatch-exception.txt", "non-standard dispatch exception"); }
    }
    const bool pdb_created = fs::is_regular_file(root / "compiler.pdb");
    const bool valid = pdb_created && clean_observation && schedule_ok && originals_ok && result.outcome() == expected
        && runner.invocations.load() == (cancel ? 2U : 3U) && downstream_ok;
    std::string observed = "[";
    for (const auto& child : active) {
        if (observed.size() != 1) observed += ',';
        observed += "{\"pid\":" + std::to_string(child.pid) + ",\"created\":" + std::to_string(child.created) + "}";
    }
    observed += ']';
    write(root / "observation.json", "{\"schema\":1,\"configuration\":" + json_string(text(config))
        + ",\"mode\":" + json_string(text(mode)) + ",\"outcome\":" + json_string(state(result.outcome()))
        + ",\"real_compiler_overlap\":" + (overlap ? "true" : "false") + ",\"retained_compilers_exited\":" + (retained_exited ? "true" : "false")
        + ",\"observed_compilers\":" + observed + ",\"observation_error\":" + json_string(observation_error)
        + ",\"compiler_pdb_created\":" + (pdb_created ? "true" : "false") + ",\"scheduling\":" + schedule + ",\"items\":" + items + ",\"compiler_invocations\":" + std::to_string(runner.invocations.load())
        + ",\"link_calls\":" + std::to_string(link_calls) + ",\"run_calls\":" + std::to_string(run_calls)
        + ",\"fixture_commits\":" + std::to_string(commits) + ",\"gate_passed\":" + (valid ? "true" : "false")
        + ",\"production_pipeline_integrated\":false,\"safe_to_transfer_write_lease\":false}\n");
    return valid ? 0 : 1;
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    try { return run(argc, argv); }
    catch (const std::exception& error) { std::cerr << "BATCH_FIXTURE_ERROR " << error.what() << '\n'; return 1; }
}
