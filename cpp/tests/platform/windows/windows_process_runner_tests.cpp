#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <chrono>
#include <fstream>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <utility>
#include <barrier>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/platform/windows/CommandLine.hpp"
#include "mqb/platform/windows/WindowsProcessRunner.hpp"
#include "mqb/process/Process.hpp"

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

[[nodiscard]] bool contains(
    const std::string& text,
    const std::string_view fragment) {
    return text.find(fragment) != std::string::npos;
}

} // namespace

namespace lifetime_tests {

namespace fs = std::filesystem;
using mqb::platform::windows::WindowsProcessRunner;
using mqb::process::ProcessSpec;
using mqb::process::ProcessResult;
using mqb::process::ProcessTermination;
using RunResult = std::expected<ProcessResult, mqb::process::ProcessError>;

struct Handle {
    HANDLE value{};
    Handle() = default;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) ::CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            if (value && value != INVALID_HANDLE_VALUE) ::CloseHandle(value);
            value = std::exchange(other.value, nullptr);
        }
        return *this;
    }
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};

fs::path self_path() {
    std::wstring path(32768, L'\0');
    const DWORD size = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (size == 0 || size >= path.size()) throw std::runtime_error{"cannot locate test executable"};
    path.resize(size);
    return fs::path{path};
}

// Helpers deliberately create a normal CreateProcess descendant. The request's
// Job, not helper-specific exit cooperation, must terminate it during the test.
std::expected<Handle, std::string> spawn_native(
    const fs::path& executable, const std::vector<std::wstring>& arguments) {
    auto command = mqb::platform::windows::build_command_line(executable.wstring(), arguments);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = ::GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = ::GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    if (!::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
                          TRUE, 0, nullptr, nullptr, &startup, &process)) {
        return std::unexpected("helper CreateProcessW failed: " + std::to_string(::GetLastError()));
    }
    Handle thread{process.hThread};
    return Handle{process.hProcess};
}

int helper_mode(const int argc, wchar_t** argv) {
    if (argc >= 2 && std::wstring_view{argv[1]} == L"--lifetime-leaf") {
        if (argc != 3) return 90;
        Handle release{::OpenEventW(SYNCHRONIZE, FALSE, argv[2])};
        if (!release) return 91;
        // Bounded fixture watchdog only. The successful cancellation test never
        // releases this event and must establish process death before this expires.
        ::WaitForSingleObject(release.value, 30000);
        return 17;
    }
    if (argc >= 2 && std::wstring_view{argv[1]} == L"--lifetime-root") {
        if (argc != 7) return 92;
        Handle ready{::OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[2])};
        Handle release{::OpenEventW(SYNCHRONIZE, FALSE, argv[3])};
        if (!ready || !release) return 93;
        auto child = spawn_native(self_path(), {L"--lifetime-leaf", argv[4]});
        if (!child) return 94;
        {
            std::ofstream ids{fs::path{argv[5]}, std::ios::trunc};
            ids << ::GetCurrentProcessId() << ' ' << ::GetProcessId(child->value) << '\n';
            ids.flush();
            if (!ids) return 95;
        }
        if (std::wstring_view{argv[6]} == L"flood") {
            std::cout << std::string(128 * 1024, 'O');
            std::cerr << std::string(128 * 1024, 'E');
        }
        BOOL in_job = FALSE;
        if (!::IsProcessInJob(::GetCurrentProcess(), nullptr, &in_job)) return 96;
        std::cout << "ROOT_READY JOB=" << in_job << '\n' << std::flush;
        std::cerr << "ROOT_DIAGNOSTIC\n" << std::flush;
        if (!::SetEvent(ready.value)) return 97;
        ::WaitForSingleObject(release.value, 25000);
        return 23;
    }
    if (argc >= 2 && std::wstring_view{argv[1]} == L"--lifetime-owner") {
        if (argc != 7) return 98;
        std::stop_source stop;
        ProcessSpec spec;
        spec.executable = self_path();
        spec.arguments = {"--lifetime-root"};
        for (int i = 2; i < argc; ++i) {
            auto value = mqb::platform::windows::utf16_to_utf8(argv[i]);
            if (!value) return 100;
            spec.arguments.push_back(std::move(*value));
        }
        spec.cancellation = stop.get_token();
        WindowsProcessRunner runner;
        auto result = runner.run(spec);
        return result ? result->exit_code : 99;
    }
    return -1;
}

struct Fixture {
    std::wstring ready_name;
    std::wstring root_release_name;
    std::wstring leaf_release_name;
    fs::path ids;
    Handle ready;
    Handle root_release;
    Handle leaf_release;

    Fixture() {
        static unsigned sequence = 0;
        const auto suffix = std::to_wstring(::GetCurrentProcessId()) + L"-"
            + std::to_wstring(::GetTickCount64()) + L"-" + std::to_wstring(++sequence);
        const auto prefix = L"Local\\MQB-lifetime-" + suffix;
        ready_name = prefix + L"-ready";
        root_release_name = prefix + L"-root";
        leaf_release_name = prefix + L"-leaf";
        ready = Handle{::CreateEventW(nullptr, TRUE, FALSE, ready_name.c_str())};
        root_release = Handle{::CreateEventW(nullptr, TRUE, FALSE, root_release_name.c_str())};
        leaf_release = Handle{::CreateEventW(nullptr, TRUE, FALSE, leaf_release_name.c_str())};
        if (!ready || !root_release || !leaf_release) throw std::runtime_error{"cannot create fixture events"};
        ids = fs::temp_directory_path() / (L"mqb-lifetime-" + suffix + L".txt");
    }
    ~Fixture() {
        release_all();
        std::error_code ignored;
        fs::remove(ids, ignored);
    }
    void release_all() const {
        ::SetEvent(root_release.value);
        ::SetEvent(leaf_release.value);
    }
    bool wait_ready() const { return ::WaitForSingleObject(ready.value, 10000) == WAIT_OBJECT_0; }
    std::vector<std::wstring> args(const std::wstring& mode, const bool flood = false) const {
        return {mode, ready_name, root_release_name, leaf_release_name, ids.wstring(), flood ? L"flood" : L"quiet"};
    }
    ProcessSpec spec(const std::stop_token token, const bool capture = true, const bool flood = false) const {
        ProcessSpec request;
        request.executable = self_path();
        for (const auto& argument : args(L"--lifetime-root", flood)) {
            auto converted = mqb::platform::windows::utf16_to_utf8(argument);
            if (!converted) throw std::runtime_error{"cannot encode fixture argument"};
            request.arguments.push_back(std::move(*converted));
        }
        request.capture_stdout = capture;
        request.capture_stderr = capture;
        request.cancellation = token;
        return request;
    }
    std::pair<Handle, Handle> processes() const {
        DWORD root = 0, leaf = 0;
        std::ifstream input{ids};
        input >> root >> leaf;
        if (!input || root == 0 || leaf == 0 || root == leaf) {
            expect(false, "fixture must publish distinct root/descendant PIDs before ready");
            return {};
        }
        // Open while both are alive and retain handles. Later checks do not
        // infer identity from a PID that might have been recycled.
        Handle root_handle{::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, root)};
        Handle leaf_handle{::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, leaf)};
        expect(root_handle && leaf_handle, "root and descendant process handles must open while alive");
        return {std::move(root_handle), std::move(leaf_handle)};
    }
};

bool ready_or_release(Fixture& fixture, std::stop_source& stop) {
    const bool ready = fixture.wait_ready();
    expect(ready, "managed helper must signal readiness without timing sleeps");
    if (!ready) {
        fixture.release_all();
        stop.request_stop();
    }
    return ready;
}

RunResult collect(std::future<RunResult>& future, Fixture& fixture) {
    const bool complete = future.wait_for(std::chrono::seconds{5}) == std::future_status::ready;
    expect(complete, "managed return must not depend on descendant releasing inherited pipes");
    if (!complete) fixture.release_all();
    auto result = future.get();
    if (!result) std::cerr << "lifetime runner error: " << result.error().message
                           << " (" << result.error().native_code << ")\n";
    return result;
}

void test_lifetime(WindowsProcessRunner& runner, const fs::path& ordinary_helper) {
    // Default and opt-in completion must retain nonzero exit and exact output.
    ProcessSpec plain;
    plain.executable = ordinary_helper;
    plain.arguments = {"two words", "", "quote\"inside", "end\\"};
    plain.environment = {{"MQB_TEST_ENV", "lifetime-isolated"}};
    auto baseline = runner.run(plain);
    std::stop_source normal;
    auto managed = plain;
    managed.cancellation = normal.get_token();
    auto completed = runner.run(managed);
    expect(baseline && completed, "default and managed normal launches must succeed");
    if (baseline && completed) {
        expect(completed->exit_code == 23 && completed->termination == ProcessTermination::exited
                   && baseline->termination == ProcessTermination::exited,
               "normal nonzero exits must not be confused with cancellation");
        expect(completed->stdout_text == baseline->stdout_text && completed->stderr_text == baseline->stderr_text,
               "managed argv/environment and stdout/stderr must match the ordinary runner");
    }
    normal.request_stop();
    managed.executable = ordinary_helper.parent_path() / "missing-pre-cancelled.exe";
    auto not_started = runner.run(managed);
    expect(not_started && not_started->termination == ProcessTermination::cancelled
               && not_started->exit_code == ERROR_CANCELLED && not_started->launch_duration.count() == 0
               && not_started->stdout_text.empty() && not_started->stderr_text.empty(),
           "pre-cancelled requests must return cancellation without launching even a missing executable");
    std::stop_source missing_stop;
    managed.cancellation = missing_stop.get_token();
    auto missing = runner.run(managed);
    expect(!missing && missing.error().code == mqb::process::ProcessErrorCode::launch_failed,
           "a live cancellable request still reports a genuine launch failure");

    for (const bool capture : {true, false}) {
        Fixture fixture;
        std::stop_source stop;
        const auto request = fixture.spec(stop.get_token(), capture, capture);
        auto running = std::async(std::launch::async, [&] { return runner.run(request); });
        if (ready_or_release(fixture, stop)) {
            auto [root, leaf] = fixture.processes();
            const auto started = std::chrono::steady_clock::now();
            stop.request_stop();
            auto result = collect(running, fixture);
            expect(result && result->termination == ProcessTermination::cancelled
                       && result->exit_code == ERROR_CANCELLED,
                   "running cancellation must be a typed nonzero outcome");
            expect(root && ::WaitForSingleObject(root.value, 0) == WAIT_OBJECT_0
                       && leaf && ::WaitForSingleObject(leaf.value, 0) == WAIT_OBJECT_0,
                   "both root and pipe-owning descendant must be dead before cancellation returns");
            if (result && capture) {
                expect(result->stdout_text.find(std::string(128 * 1024, 'O')) == 0
                           && result->stdout_text.find("ROOT_READY JOB=1") != std::string::npos
                           && result->stderr_text.find(std::string(128 * 1024, 'E')) == 0
                           && result->stderr_text.find("ROOT_DIAGNOSTIC") != std::string::npos,
                       "cancellation must drain already-written output and preserve stream separation");
            } else if (result) {
                expect(result->stdout_text.empty() && result->stderr_text.empty(),
                       "no-capture cancellation must not silently enable capture");
            }
            std::cout << "cancel capture=" << capture << " ms="
                      << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count()
                      << '\n';
        } else {
            (void)collect(running, fixture);
        }
    }
    {
        Fixture fixture;
        std::stop_source stop;
        const auto request = fixture.spec(stop.get_token());
        auto running = std::async(std::launch::async, [&] { return runner.run(request); });
        if (ready_or_release(fixture, stop)) {
            auto [root, leaf] = fixture.processes();
            ::SetEvent(fixture.root_release.value); // The leaf is intentionally not released.
            auto result = collect(running, fixture);
            expect(result && result->termination == ProcessTermination::exited && result->exit_code == 23,
                   "managed normal root exit must retain its exit code while ending descendant lifetime");
            expect(root && ::WaitForSingleObject(root.value, 0) == WAIT_OBJECT_0
                       && leaf && ::WaitForSingleObject(leaf.value, 0) == WAIT_OBJECT_0,
                   "root completion may not return with a managed pipe-owning descendant alive");
        } else {
            (void)collect(running, fixture);
        }
    }
    {
        Fixture a, b;
        std::stop_source stop_a, stop_b;
        const auto request_a = a.spec(stop_a.get_token());
        const auto request_b = b.spec(stop_b.get_token());
        auto running_a = std::async(std::launch::async, [&] { return runner.run(request_a); });
        auto running_b = std::async(std::launch::async, [&] { return runner.run(request_b); });
        const bool ready_a = ready_or_release(a, stop_a);
        const bool ready_b = ready_or_release(b, stop_b);
        if (ready_a && ready_b) {
            auto [root_b, leaf_b] = b.processes();
            stop_a.request_stop();
            auto result_a = collect(running_a, a);
            expect(result_a && result_a->termination == ProcessTermination::cancelled,
                   "first concurrent invocation must be cancelled");
            expect(running_b.wait_for(std::chrono::milliseconds{0}) == std::future_status::timeout
                       && root_b && ::WaitForSingleObject(root_b.value, 0) == WAIT_TIMEOUT
                       && leaf_b && ::WaitForSingleObject(leaf_b.value, 0) == WAIT_TIMEOUT,
                   "cancelling one invocation must not kill another on the same runner");
            ::SetEvent(b.root_release.value);
            auto result_b = collect(running_b, b);
            expect(result_b && result_b->termination == ProcessTermination::exited && result_b->exit_code == 23,
                   "uncancelled concurrent invocation must complete normally");
        } else {
            a.release_all(); b.release_all(); stop_a.request_stop(); stop_b.request_stop();
            (void)collect(running_a, a); (void)collect(running_b, b);
        }
    }
    {
        Fixture fixture;
        auto owner = spawn_native(self_path(), fixture.args(L"--lifetime-owner"));
        expect(owner.has_value(), "managed-owner crash fixture must launch");
        if (owner) {
            const bool ready = fixture.wait_ready();
            expect(ready, "crash fixture must signal only after its managed tree exists");
            auto processes = ready ? fixture.processes() : std::pair<Handle, Handle>{};
            const BOOL killed = ::TerminateProcess(owner->value, 87);
            expect(killed != FALSE && ::WaitForSingleObject(owner->value, 5000) == WAIT_OBJECT_0,
                   "fixture owner must be terminated through its retained process handle");
            if (ready) {
                expect(processes.first && ::WaitForSingleObject(processes.first.value, 5000) == WAIT_OBJECT_0
                           && processes.second && ::WaitForSingleObject(processes.second.value, 5000) == WAIT_OBJECT_0,
                       "owner death must close the unshared Job and terminate its root and descendant");
            }
        }
    }
    {
        DWORD before = 0, after = 0;
        expect(::GetProcessHandleCount(::GetCurrentProcess(), &before) != FALSE, "initial handle count must be queryable");
        for (int i = 0; i < 24; ++i) {
            std::stop_source stop;
            auto request = plain;
            request.cancellation = stop.get_token();
            if (i % 2 != 0) stop.request_stop();
            auto result = runner.run(request);
            expect(result && result->termination == (i % 2 ? ProcessTermination::cancelled : ProcessTermination::exited),
                   "repeated normal and pre-cancelled calls must preserve termination identity");
        }
        expect(::GetProcessHandleCount(::GetCurrentProcess(), &after) != FALSE, "final handle count must be queryable");
        expect(after <= before + 2, "repeated managed requests must not leak Job/event/process/pipe handles");
    }
    std::cout << "request-owned process lifetime checks passed\n";
}

} // namespace lifetime_tests

int wmain(const int argc, wchar_t** argv) {
    const int helper_result = lifetime_tests::helper_mode(argc, argv);
    if (helper_result >= 0) return helper_result;
    if (argc != 2) {
        std::cerr << "usage: mqb_windows_process_runner_tests <helper.exe>\n";
        return 2;
    }

    const std::filesystem::path helper = std::filesystem::absolute(argv[1]);
    mqb::platform::windows::WindowsProcessRunner runner;

    mqb::process::ProcessSpec spec;
    spec.executable = helper;
    spec.arguments = {
        "alpha",
        "two words",
        "quote\"inside",
        "",
        "ends-with-backslash\\",
    };
    spec.working_directory = helper.parent_path();
    spec.environment = {
        mqb::process::EnvironmentVariable{"MQB_TEST_ENV", "hello world"},
    };

    const auto result = runner.run(spec);
    expect(result.has_value(), "valid process specification should launch successfully");
    if (result) {
        expect(result->exit_code == 23,
               "non-zero child exit code should be returned as data, not a launch error");
        expect(contains(result->stdout_text, "ARGC=5"),
               "child should receive every structured argv element");
        expect(contains(result->stdout_text, "ARG=[alpha]"),
               "simple argument should reach the child unchanged");
        expect(contains(result->stdout_text, "ARG=[two words]"),
               "argument containing spaces should reach the child unchanged");
        expect(contains(result->stdout_text, "ARG=[quote\"inside]"),
               "argument containing a quote should reach the child unchanged");
        expect(contains(result->stdout_text, "ARG=[]"),
               "empty argument should survive process creation");
        expect(contains(result->stdout_text, "ARG=[ends-with-backslash\\]"),
               "trailing backslash should survive process creation");
        expect(contains(result->stdout_text, "ENV=[hello world]"),
               "environment override should reach the child");
        expect(contains(result->stderr_text, "STDERR-MARKER"),
               "stderr should be captured separately from stdout");
    }

    expect(_putenv_s("MQB_TEST_ENV", "parent-only") == 0,
           "test should be able to seed an inherited parent environment variable");
    mqb::process::ProcessSpec removed_environment = spec;
    removed_environment.arguments = {"removed"};
    removed_environment.environment = {
        mqb::process::EnvironmentVariable{"MQB_TEST_ENV", {}, true},
    };
    const auto removed_result = runner.run(removed_environment);
    expect(removed_result.has_value(),
           "process should launch when an inherited environment variable is removed");
    if (removed_result) {
        expect(contains(removed_result->stdout_text, "ENV=[<missing>]"),
               "remove=true must make an inherited variable absent, not merely empty");
    }
    expect(_putenv_s("MQB_TEST_ENV", "") == 0,
           "test should clean up the seeded parent environment variable");

    mqb::process::ProcessSpec isolated_environment = spec;
    isolated_environment.arguments = {"isolated"};
    isolated_environment.inherit_environment = false;
    isolated_environment.environment = {
        mqb::process::EnvironmentVariable{"MQB_TEST_ENV", "isolated-value"},
    };
    const auto isolated_result = runner.run(isolated_environment);
    expect(isolated_result.has_value(),
           "process should launch with an explicit non-inherited environment block");
    if (isolated_result) {
        expect(contains(isolated_result->stdout_text, "ENV=[isolated-value]"),
               "explicit environment block should retain requested variables");
    }

    mqb::process::ProcessSpec flood;
    flood.executable = helper;
    flood.arguments = {"--flood"};
    const auto flood_result = runner.run(flood);
    expect(flood_result.has_value(),
           "runner should drain stdout and stderr concurrently without pipe deadlock");
    if (flood_result) {
        constexpr std::size_t payload_size = 256 * 1024;
        expect(flood_result->exit_code == 17,
               "flood helper exit code should be preserved");
        expect(flood_result->stdout_text.size() == payload_size,
               "large stdout payload should be captured completely");
        expect(flood_result->stderr_text.size() == payload_size,
               "large stderr payload should be captured completely");
    }

    {
        constexpr int launch_count = 12;
        std::barrier start_gate{launch_count};
        std::vector<std::future<std::expected<mqb::process::ProcessResult, mqb::process::ProcessError>>>
            launches;
        launches.reserve(launch_count);

        for (int index = 0; index < launch_count; ++index) {
            launches.push_back(std::async(
                std::launch::async,
                [&runner, &start_gate, helper, index] {
                    mqb::process::ProcessSpec concurrent;
                    concurrent.executable = helper;
                    concurrent.arguments = {"concurrent-" + std::to_string(index)};
                    concurrent.environment = {
                        mqb::process::EnvironmentVariable{
                            "MQB_TEST_ENV",
                            "env-" + std::to_string(index)},
                    };
                    start_gate.arrive_and_wait();
                    return runner.run(concurrent);
                }));
        }

        for (int index = 0; index < launch_count; ++index) {
            auto concurrent = launches[static_cast<std::size_t>(index)].get();
            expect(concurrent.has_value(),
                   "same WindowsProcessRunner instance should support concurrent launches");
            if (!concurrent) {
                continue;
            }
            expect(concurrent->exit_code == 23,
                   "concurrent child exit code should remain isolated");
            expect(contains(
                       concurrent->stdout_text,
                       "ARG=[concurrent-" + std::to_string(index) + "]"),
                   "concurrent stdout capture should belong to the correct child");
            expect(contains(
                       concurrent->stdout_text,
                       "ENV=[env-" + std::to_string(index) + "]"),
                   "concurrent environment blocks should remain isolated");
            expect(contains(concurrent->stderr_text, "STDERR-MARKER"),
                   "concurrent stderr capture should complete independently");
        }
    }

    mqb::process::ProcessSpec empty_executable;
    const auto empty_result = runner.run(empty_executable);
    expect(!empty_result.has_value(), "empty executable should be rejected before launch");
    if (!empty_result) {
        expect(empty_result.error().code == mqb::process::ProcessErrorCode::invalid_specification,
               "empty executable should report invalid_specification");
    }

    mqb::process::ProcessSpec invalid_environment;
    invalid_environment.executable = helper;
    invalid_environment.environment = {
        mqb::process::EnvironmentVariable{"BAD=NAME", "value"},
    };
    const auto invalid_environment_result = runner.run(invalid_environment);
    expect(!invalid_environment_result.has_value(),
           "invalid environment variable name should be rejected");
    if (!invalid_environment_result) {
        expect(invalid_environment_result.error().code
                   == mqb::process::ProcessErrorCode::invalid_specification,
               "invalid environment name should report invalid_specification");
    }

    mqb::process::ProcessSpec missing_executable;
    missing_executable.executable = helper.parent_path() / "definitely-missing-mqb-helper.exe";
    const auto missing_result = runner.run(missing_executable);
    expect(!missing_result.has_value(), "missing executable should fail at launch");
    if (!missing_result) {
        expect(missing_result.error().code == mqb::process::ProcessErrorCode::launch_failed,
               "missing executable should report launch_failed");
        expect(missing_result.error().native_code != 0,
               "launch failure should preserve the Windows error code");
    }

    lifetime_tests::test_lifetime(runner, helper);

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "mqb_windows_process_runner_tests passed\n";
    return 0;
}
