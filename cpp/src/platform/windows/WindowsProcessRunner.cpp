#include "mqb/platform/windows/WindowsProcessRunner.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "mqb/core/PerformanceEvidence.hpp"
#include "mqb/platform/windows/CommandLine.hpp"

namespace mqb::platform::windows {
namespace {

using process::ProcessError;
using process::ProcessErrorCode;

class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}

    ~UniqueHandle() {
        reset();
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)) {}

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset(std::exchange(other.handle_, nullptr));
        }
        return *this;
    }

    [[nodiscard]] HANDLE get() const noexcept {
        return handle_;
    }

    [[nodiscard]] bool valid() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    void reset(HANDLE handle = nullptr) noexcept {
        if (valid()) {
            ::CloseHandle(handle_);
        }
        handle_ = handle;
    }

private:
    HANDLE handle_{nullptr};
};

class ProcThreadAttributeList {
public:
    ProcThreadAttributeList() = default;

    ~ProcThreadAttributeList() {
        reset();
    }

    ProcThreadAttributeList(const ProcThreadAttributeList&) = delete;
    ProcThreadAttributeList& operator=(const ProcThreadAttributeList&) = delete;

    ProcThreadAttributeList(ProcThreadAttributeList&& other) noexcept
        : list_(std::exchange(other.list_, nullptr)) {}

    ProcThreadAttributeList& operator=(ProcThreadAttributeList&& other) noexcept {
        if (this != &other) {
            reset();
            list_ = std::exchange(other.list_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST get() const noexcept {
        return list_;
    }

    [[nodiscard]] static std::expected<ProcThreadAttributeList, ProcessError>
    for_launch(std::span<HANDLE> handles, std::span<HANDLE> jobs);

private:
    explicit ProcThreadAttributeList(LPPROC_THREAD_ATTRIBUTE_LIST list) noexcept
        : list_(list) {}

    void reset() noexcept {
        if (list_ != nullptr) {
            ::DeleteProcThreadAttributeList(list_);
            ::HeapFree(::GetProcessHeap(), 0, list_);
            list_ = nullptr;
        }
    }

    LPPROC_THREAD_ATTRIBUTE_LIST list_{nullptr};
};

class EnvironmentStrings {
public:
    explicit EnvironmentStrings(LPWCH block) noexcept : block_(block) {}

    ~EnvironmentStrings() {
        if (block_ != nullptr) {
            ::FreeEnvironmentStringsW(block_);
        }
    }

    EnvironmentStrings(const EnvironmentStrings&) = delete;
    EnvironmentStrings& operator=(const EnvironmentStrings&) = delete;

    [[nodiscard]] LPWCH get() const noexcept {
        return block_;
    }

private:
    LPWCH block_{nullptr};
};

struct PipePair {
    UniqueHandle read;
    UniqueHandle write;
};

struct EnvironmentEntry {
    std::wstring name;
    std::wstring value;
};

[[nodiscard]] ProcessError error(
    const ProcessErrorCode code,
    const DWORD native_code,
    std::string message) {
    return ProcessError{
        .code = code,
        .native_code = native_code,
        .message = std::move(message),
    };
}

std::expected<ProcThreadAttributeList, ProcessError>
ProcThreadAttributeList::for_launch(
    const std::span<HANDLE> handles,
    const std::span<HANDLE> jobs) {
    const DWORD count = static_cast<DWORD>(!handles.empty()) + static_cast<DWORD>(!jobs.empty());
    if (count == 0) {
        return std::unexpected(error(
            ProcessErrorCode::invalid_specification,
            ERROR_INVALID_PARAMETER,
            "process attribute list is empty"));
    }

    SIZE_T bytes = 0;
    ::InitializeProcThreadAttributeList(nullptr, count, 0, &bytes);
    if (bytes == 0) {
        return std::unexpected(error(
            ProcessErrorCode::launch_failed,
            ::GetLastError(),
            "failed to size process attribute list"));
    }

    auto* raw = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
        ::HeapAlloc(::GetProcessHeap(), 0, bytes));
    if (raw == nullptr) {
        return std::unexpected(error(
            ProcessErrorCode::launch_failed,
            ERROR_NOT_ENOUGH_MEMORY,
            "failed to allocate process attribute list"));
    }

    if (!::InitializeProcThreadAttributeList(raw, count, 0, &bytes)) {
        const DWORD native_code = ::GetLastError();
        ::HeapFree(::GetProcessHeap(), 0, raw);
        return std::unexpected(error(
            ProcessErrorCode::launch_failed,
            native_code,
            "InitializeProcThreadAttributeList failed"));
    }

    ProcThreadAttributeList result{raw};
    if (!handles.empty() && !::UpdateProcThreadAttribute(
            result.get(),
            0,
            PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            handles.data(),
            handles.size_bytes(),
            nullptr,
            nullptr)) {
        return std::unexpected(error(
            ProcessErrorCode::launch_failed,
            ::GetLastError(),
            "UpdateProcThreadAttribute failed for inherited handle whitelist"));
    }
    // Assignment is part of CreateProcess, before the initial thread can run.
    // A post-launch AssignProcessToJobObject would leave a child-escape window.
    if (!jobs.empty() && !::UpdateProcThreadAttribute(
            result.get(), 0, PROC_THREAD_ATTRIBUTE_JOB_LIST,
            jobs.data(), jobs.size_bytes(), nullptr, nullptr)) {
        return std::unexpected(error(
            ProcessErrorCode::launch_failed, ::GetLastError(),
            "UpdateProcThreadAttribute failed for request-owned job"));
    }
    return result;
}

struct SignalCancellation {
    HANDLE event;
    void operator()() const noexcept { ::SetEvent(event); }
};

[[nodiscard]] process::ProcessResult cancelled_result() {
    return process::ProcessResult{
        .exit_code = static_cast<int>(ERROR_CANCELLED),
        .termination = process::ProcessTermination::cancelled,
    };
}

// Owns only this launch's unnamed, non-inheritable Job. No breakaway flags.
// Setup failure must not silently fall back to an unmanaged process.
[[nodiscard]] std::expected<UniqueHandle, ProcessError> create_process_job() {
    UniqueHandle job{::CreateJobObjectW(nullptr, nullptr)};
    if (!job.valid()) {
        return std::unexpected(error(
            ProcessErrorCode::launch_failed, ::GetLastError(), "CreateJobObjectW failed"));
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!::SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation,
                                  &limits, sizeof(limits))) {
        return std::unexpected(error(
            ProcessErrorCode::launch_failed, ::GetLastError(),
            "failed to set request-owned job limits"));
    }
    return job;
}

// Snapshot live members before requesting termination. ActiveProcesses alone
// can reach zero before a retained descendant process handle becomes signaled.
// Only the Job is terminated; PIDs are used to open and verify identity, never
// to terminate an arbitrary (potentially recycled) process.
[[nodiscard]] std::expected<std::vector<UniqueHandle>, ProcessError>
retain_job_members(const HANDLE job) {
    constexpr std::size_t max_members = 65536;
    std::size_t capacity = 64;
    for (;;) {
        // ULONG_PTR storage provides alignment for the variable-length Win32
        // structure. Two slots safely cover its two DWORD counts on x86/x64.
        std::vector<ULONG_PTR> storage(capacity + 2);
        auto* members = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(storage.data());
        const BOOL queried = ::QueryInformationJobObject(
            job, JobObjectBasicProcessIdList, members,
            static_cast<DWORD>(offsetof(JOBOBJECT_BASIC_PROCESS_ID_LIST, ProcessIdList)
                               + capacity * sizeof(ULONG_PTR)), nullptr);
        if (!queried) {
            const DWORD native_code = ::GetLastError();
            if (native_code != ERROR_MORE_DATA) {
                return std::unexpected(error(
                    ProcessErrorCode::wait_failed, native_code,
                    "failed to enumerate request-owned job members"));
            }
        }
        if (!queried || members->NumberOfAssignedProcesses > members->NumberOfProcessIdsInList) {
            if (capacity == max_members) {
                return std::unexpected(error(
                    ProcessErrorCode::wait_failed, ERROR_NOT_ENOUGH_MEMORY,
                    "request-owned job exceeds the cleanup census limit"));
            }
            capacity = std::min(capacity * 2, max_members);
            continue;
        }
        if (members->NumberOfProcessIdsInList > capacity) {
            return std::unexpected(error(
                ProcessErrorCode::wait_failed, ERROR_INVALID_DATA,
                "invalid request-owned job member count"));
        }
        std::vector<UniqueHandle> retained;
        retained.reserve(members->NumberOfProcessIdsInList);
        for (DWORD index = 0; index < members->NumberOfProcessIdsInList; ++index) {
            const auto pid = static_cast<DWORD>(members->ProcessIdList[index]);
            UniqueHandle process_handle{::OpenProcess(
                SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
            if (!process_handle.valid()) {
                const DWORD native_code = ::GetLastError();
                // The object no longer exists, not merely a liveness guess.
                if (native_code == ERROR_INVALID_PARAMETER) continue;
                return std::unexpected(error(
                    ProcessErrorCode::wait_failed, native_code,
                    "failed to retain a request-owned job member"));
            }
            BOOL belongs = FALSE;
            if (!::IsProcessInJob(process_handle.get(), job, &belongs)) {
                return std::unexpected(error(
                    ProcessErrorCode::wait_failed, ::GetLastError(),
                    "failed to verify request-owned process identity"));
            }
            // An unrelated replacement PID cannot be waited on or terminated.
            if (belongs) retained.push_back(std::move(process_handle));
        }
        return retained;
    }
}

[[nodiscard]] std::expected<void, ProcessError> terminate_and_drain_job(
    const HANDLE job, const DWORD exit_code) {
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION before{};
    if (!::QueryInformationJobObject(job, JobObjectBasicAccountingInformation,
                                    &before, sizeof(before), nullptr)) {
        return std::unexpected(error(
            ProcessErrorCode::wait_failed, ::GetLastError(),
            "failed to establish the process cleanup census"));
    }
    auto members = retain_job_members(job);
    if (!members) return std::unexpected(members.error());
    if (!::TerminateJobObject(job, exit_code)) {
        return std::unexpected(error(
            ProcessErrorCode::wait_failed, ::GetLastError(), "TerminateJobObject failed"));
    }
    for (const auto& member : *members) {
        if (::WaitForSingleObject(member.get(), INFINITE) != WAIT_OBJECT_0) {
            return std::unexpected(error(
                ProcessErrorCode::wait_failed, ::GetLastError(),
                "failed to confirm request-owned process termination"));
        }
    }
    for (;;) {
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION after{};
        if (!::QueryInformationJobObject(job, JobObjectBasicAccountingInformation,
                                        &after, sizeof(after), nullptr)) {
            return std::unexpected(error(
                ProcessErrorCode::wait_failed, ::GetLastError(),
                "failed to establish request-owned job quiescence"));
        }
        // A creation race invalidates this census, even if that process is
        // already absent from the active list. Fail closed, not a false success
        // based on an empty queue, an arbitrary sleep or best-effort Job messages.
        if (after.TotalProcesses != before.TotalProcesses) {
            return std::unexpected(error(
                ProcessErrorCode::wait_failed, ERROR_RETRY,
                "job membership changed during cleanup; quiescence is unproven"));
        }
        if (after.ActiveProcesses == 0) return {};
        // Cleanup-only wait, never a no-op/background polling thread. Kernel
        // teardown has no promised wall-clock upper bound.
        ::Sleep(1);
    }
}

[[nodiscard]] bool contains_nul(const std::string_view value) noexcept {
    return value.find('\0') != std::string_view::npos;
}

[[nodiscard]] bool contains_nul(const std::wstring_view value) noexcept {
    return value.find(L'\0') != std::wstring_view::npos;
}

[[nodiscard]] std::expected<std::wstring, ProcessError> utf8_to_wide(
    const std::string_view value,
    const std::string_view field_name) {
    if (contains_nul(value)) {
        return std::unexpected(error(
            ProcessErrorCode::invalid_specification,
            ERROR_INVALID_PARAMETER,
            std::string{field_name} + " contains an embedded NUL"));
    }

    if (value.empty()) {
        return std::wstring{};
    }

    const int required = ::MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (required == 0) {
        return std::unexpected(error(
            ProcessErrorCode::invalid_specification,
            ::GetLastError(),
            std::string{field_name} + " is not valid UTF-8"));
    }

    std::wstring result(static_cast<std::size_t>(required), L'\0');
    const int converted = ::MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        required);
    if (converted != required) {
        return std::unexpected(error(
            ProcessErrorCode::invalid_specification,
            ::GetLastError(),
            std::string{"failed to convert "} + std::string{field_name} + " from UTF-8"));
    }
    return result;
}

[[nodiscard]] std::expected<PipePair, ProcessError> create_capture_pipe() {
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;

    HANDLE read_handle = nullptr;
    HANDLE write_handle = nullptr;
    if (!::CreatePipe(&read_handle, &write_handle, &attributes, 0)) {
        return std::unexpected(error(
            ProcessErrorCode::launch_failed,
            ::GetLastError(),
            "CreatePipe failed"));
    }

    PipePair pipe{UniqueHandle{read_handle}, UniqueHandle{write_handle}};
    if (!::SetHandleInformation(pipe.read.get(), HANDLE_FLAG_INHERIT, 0)) {
        return std::unexpected(error(
            ProcessErrorCode::launch_failed,
            ::GetLastError(),
            "SetHandleInformation failed for capture pipe"));
    }
    return pipe;
}

[[nodiscard]] std::expected<UniqueHandle, ProcessError> inheritable_standard_handle(
    const DWORD standard_handle_id,
    const DWORD fallback_access) {
    HANDLE source = ::GetStdHandle(standard_handle_id);
    if (source != nullptr && source != INVALID_HANDLE_VALUE) {
        HANDLE duplicate = nullptr;
        if (::DuplicateHandle(
                ::GetCurrentProcess(),
                source,
                ::GetCurrentProcess(),
                &duplicate,
                0,
                TRUE,
                DUPLICATE_SAME_ACCESS)) {
            return UniqueHandle{duplicate};
        }
        return std::unexpected(error(
            ProcessErrorCode::launch_failed,
            ::GetLastError(),
            "DuplicateHandle failed for a standard stream"));
    }

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    HANDLE null_handle = ::CreateFileW(
        L"NUL",
        fallback_access,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &attributes,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (null_handle == INVALID_HANDLE_VALUE) {
        return std::unexpected(error(
            ProcessErrorCode::launch_failed,
            ::GetLastError(),
            "failed to open NUL for a missing standard stream"));
    }
    return UniqueHandle{null_handle};
}

[[nodiscard]] bool environment_name_equal(
    const std::wstring& left,
    const std::wstring& right) noexcept {
    return ::CompareStringOrdinal(
               left.data(),
               static_cast<int>(left.size()),
               right.data(),
               static_cast<int>(right.size()),
               TRUE)
        == CSTR_EQUAL;
}

[[nodiscard]] bool environment_name_less(
    const EnvironmentEntry& left,
    const EnvironmentEntry& right) noexcept {
    const int comparison = ::CompareStringOrdinal(
        left.name.data(),
        static_cast<int>(left.name.size()),
        right.name.data(),
        static_cast<int>(right.name.size()),
        TRUE);
    if (comparison == CSTR_LESS_THAN) {
        return true;
    }
    if (comparison == CSTR_GREATER_THAN) {
        return false;
    }
    return left.name < right.name;
}

[[nodiscard]] std::expected<std::vector<EnvironmentEntry>, ProcessError>
read_inherited_environment() {
    std::vector<EnvironmentEntry> entries;
    EnvironmentStrings block{::GetEnvironmentStringsW()};
    if (block.get() == nullptr) {
        return std::unexpected(error(
            ProcessErrorCode::launch_failed,
            ::GetLastError(),
            "GetEnvironmentStringsW failed"));
    }

    for (const wchar_t* cursor = block.get(); *cursor != L'\0';) {
        const std::wstring entry{cursor};
        cursor += entry.size() + 1;

        const std::size_t search_from = !entry.empty() && entry.front() == L'=' ? 1 : 0;
        const std::size_t delimiter = entry.find(L'=', search_from);
        if (delimiter == std::wstring::npos) {
            continue;
        }

        entries.push_back(EnvironmentEntry{
            .name = entry.substr(0, delimiter),
            .value = entry.substr(delimiter + 1),
        });
    }

    return entries;
}

[[nodiscard]] std::expected<std::vector<wchar_t>, ProcessError> build_environment_block(
    const process::ProcessSpec& spec) {
    std::vector<EnvironmentEntry> entries;
    if (spec.inherit_environment) {
        auto inherited = read_inherited_environment();
        if (!inherited) {
            return std::unexpected(inherited.error());
        }
        entries = std::move(*inherited);
    }

    for (const auto& override_variable : spec.environment) {
        if (override_variable.name.empty()
            || override_variable.name.find('=') != std::string::npos
            || contains_nul(override_variable.name)
            || contains_nul(override_variable.value)) {
            return std::unexpected(error(
                ProcessErrorCode::invalid_specification,
                ERROR_INVALID_PARAMETER,
                "environment variable name/value is invalid"));
        }

        auto wide_name = utf8_to_wide(override_variable.name, "environment variable name");
        if (!wide_name) {
            return std::unexpected(wide_name.error());
        }

        const auto existing = std::find_if(
            entries.begin(),
            entries.end(),
            [&wide_name](const EnvironmentEntry& entry) {
                return environment_name_equal(entry.name, *wide_name);
            });
        if (override_variable.remove) {
            if (existing != entries.end()) {
                entries.erase(existing);
            }
            continue;
        }

        auto wide_value = utf8_to_wide(override_variable.value, "environment variable value");
        if (!wide_value) {
            return std::unexpected(wide_value.error());
        }
        if (existing != entries.end()) {
            existing->name = std::move(*wide_name);
            existing->value = std::move(*wide_value);
        } else {
            entries.push_back(EnvironmentEntry{
                .name = std::move(*wide_name),
                .value = std::move(*wide_value),
            });
        }
    }

    std::sort(entries.begin(), entries.end(), environment_name_less);

    std::vector<wchar_t> block;
    for (const auto& entry : entries) {
        block.insert(block.end(), entry.name.begin(), entry.name.end());
        block.push_back(L'=');
        block.insert(block.end(), entry.value.begin(), entry.value.end());
        block.push_back(L'\0');
    }

    if (entries.empty()) {
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

void read_pipe(HANDLE handle, std::string& output, std::optional<DWORD>& read_error) {
    char buffer[4096];
    for (;;) {
        DWORD bytes_read = 0;
        if (::ReadFile(handle, buffer, sizeof(buffer), &bytes_read, nullptr)) {
            if (bytes_read == 0) {
                return;
            }
            output.append(buffer, static_cast<std::size_t>(bytes_read));
            continue;
        }

        const DWORD native_code = ::GetLastError();
        if (native_code != ERROR_BROKEN_PIPE) {
            read_error = native_code;
        }
        return;
    }
}

[[nodiscard]] bool ascii_starts_with_ignore_case(
    const std::string_view value,
    const std::string_view prefix) noexcept {
    if (value.size() < prefix.size()) return false;
    for (std::size_t index = 0; index < prefix.size(); ++index) {
        unsigned char left = static_cast<unsigned char>(value[index]);
        unsigned char right = static_cast<unsigned char>(prefix[index]);
        if (left >= 'A' && left <= 'Z') left = static_cast<unsigned char>(left + ('a' - 'A'));
        if (right >= 'A' && right <= 'Z') right = static_cast<unsigned char>(right + ('a' - 'A'));
        if (left != right) return false;
    }
    return true;
}

struct InstrumentedProcess {
    mqb::performance::ProcessKind process_kind;
    mqb::performance::WorkKind work_kind;
};

[[nodiscard]] std::optional<InstrumentedProcess> classify_instrumented_process(
    const process::ProcessSpec& spec) {
    const std::string executable =
        mqb::platform::windows::path_identity_key(spec.executable.filename());
    if (executable == "link.exe") {
        return InstrumentedProcess{
            .process_kind = mqb::performance::ProcessKind::linker,
            .work_kind = mqb::performance::WorkKind::link_execution,
        };
    }
    if (executable == "lib.exe") {
        return InstrumentedProcess{
            .process_kind = mqb::performance::ProcessKind::librarian,
            .work_kind = mqb::performance::WorkKind::archive_execution,
        };
    }
    if (executable != "cl.exe") return std::nullopt;

    const bool dependency_scan = std::any_of(
        spec.arguments.begin(),
        spec.arguments.end(),
        [](const std::string& argument) {
            return ascii_starts_with_ignore_case(argument, "/scanDependencies")
                || ascii_starts_with_ignore_case(argument, "-scanDependencies");
        });
    return InstrumentedProcess{
        .process_kind = mqb::performance::ProcessKind::compiler,
        .work_kind = dependency_scan
            ? mqb::performance::WorkKind::module_scan_execution
            : mqb::performance::WorkKind::compile_execution,
    };
}

} // namespace

std::expected<process::ProcessResult, process::ProcessError>
WindowsProcessRunner::run(const process::ProcessSpec& spec) {
    if (spec.executable.empty()) {
        return std::unexpected(error(
            ProcessErrorCode::invalid_specification,
            ERROR_INVALID_PARAMETER,
            "process executable is empty"));
    }

    std::wstring executable = spec.executable.wstring();
    if (contains_nul(executable)) {
        return std::unexpected(error(
            ProcessErrorCode::invalid_specification,
            ERROR_INVALID_PARAMETER,
            "process executable contains an embedded NUL"));
    }

    std::vector<std::wstring> arguments;
    arguments.reserve(spec.arguments.size());
    for (const auto& argument : spec.arguments) {
        auto converted = utf8_to_wide(argument, "process argument");
        if (!converted) {
            return std::unexpected(converted.error());
        }
        arguments.push_back(std::move(*converted));
    }
    std::wstring command_line = build_command_line(executable, arguments);

    std::optional<std::wstring> working_directory;
    if (spec.working_directory) {
        working_directory = spec.working_directory->wstring();
        if (contains_nul(*working_directory)) {
            return std::unexpected(error(
                ProcessErrorCode::invalid_specification,
                ERROR_INVALID_PARAMETER,
                "working directory contains an embedded NUL"));
        }
    }

    std::vector<wchar_t> environment_block;
    LPVOID environment_pointer = nullptr;
    DWORD creation_flags = 0;
    if (!spec.environment.empty() || !spec.inherit_environment) {
        auto built_environment = build_environment_block(spec);
        if (!built_environment) {
            return std::unexpected(built_environment.error());
        }
        environment_block = std::move(*built_environment);
        environment_pointer = environment_block.data();
        creation_flags |= CREATE_UNICODE_ENVIRONMENT;
    }

    if (spec.cancellation.stop_requested()) return cancelled_result();

    UniqueHandle process_job;
    UniqueHandle cancellation_event;
    if (spec.cancellation.stop_possible()) {
        auto job = create_process_job();
        if (!job) return std::unexpected(job.error());
        process_job = std::move(*job);
        cancellation_event.reset(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!cancellation_event.valid()) {
            return std::unexpected(error(
                ProcessErrorCode::launch_failed, ::GetLastError(),
                "failed to create process cancellation event"));
        }
    }
    // The callback is destroyed before its event; stop requests never touch
    // handles that have already been closed or borrow another invocation's state.
    std::optional<std::stop_callback<SignalCancellation>> cancellation_callback;
    if (cancellation_event.valid()) {
        cancellation_callback.emplace(spec.cancellation, SignalCancellation{cancellation_event.get()});
    }

    std::optional<PipePair> stdout_pipe;
    std::optional<PipePair> stderr_pipe;
    if (spec.capture_stdout) {
        auto pipe = create_capture_pipe();
        if (!pipe) {
            return std::unexpected(pipe.error());
        }
        stdout_pipe.emplace(std::move(*pipe));
    }
    if (spec.capture_stderr) {
        auto pipe = create_capture_pipe();
        if (!pipe) {
            return std::unexpected(pipe.error());
        }
        stderr_pipe.emplace(std::move(*pipe));
    }

    STARTUPINFOW basic_startup{};
    basic_startup.cb = sizeof(basic_startup);
    STARTUPINFOEXW extended_startup{};
    extended_startup.StartupInfo.cb = sizeof(extended_startup);
    LPSTARTUPINFOW startup = &basic_startup;

    UniqueHandle child_stdin;
    UniqueHandle child_stdout;
    UniqueHandle child_stderr;
    std::vector<HANDLE> inherited_handles;
    std::vector<HANDLE> process_jobs;
    if (process_job.valid()) process_jobs.push_back(process_job.get());
    // Attribute values must outlive the attribute list, not merely its creation.
    std::optional<ProcThreadAttributeList> attribute_list;

    const bool use_explicit_standard_handles = spec.capture_stdout || spec.capture_stderr;
    if (use_explicit_standard_handles) {
        auto stdin_handle = inheritable_standard_handle(STD_INPUT_HANDLE, GENERIC_READ);
        if (!stdin_handle) {
            return std::unexpected(stdin_handle.error());
        }
        child_stdin = std::move(*stdin_handle);

        if (!spec.capture_stdout) {
            auto stdout_handle = inheritable_standard_handle(STD_OUTPUT_HANDLE, GENERIC_WRITE);
            if (!stdout_handle) {
                return std::unexpected(stdout_handle.error());
            }
            child_stdout = std::move(*stdout_handle);
        }
        if (!spec.capture_stderr) {
            auto stderr_handle = inheritable_standard_handle(STD_ERROR_HANDLE, GENERIC_WRITE);
            if (!stderr_handle) {
                return std::unexpected(stderr_handle.error());
            }
            child_stderr = std::move(*stderr_handle);
        }

        auto& startup_info = extended_startup.StartupInfo;
        startup_info.dwFlags |= STARTF_USESTDHANDLES;
        startup_info.hStdInput = child_stdin.get();
        startup_info.hStdOutput = spec.capture_stdout ? stdout_pipe->write.get() : child_stdout.get();
        startup_info.hStdError = spec.capture_stderr ? stderr_pipe->write.get() : child_stderr.get();

        inherited_handles = {
            startup_info.hStdInput,
            startup_info.hStdOutput,
            startup_info.hStdError,
        };
    }
    if (use_explicit_standard_handles || process_job.valid()) {
        auto attributes = ProcThreadAttributeList::for_launch(inherited_handles, process_jobs);
        if (!attributes) {
            return std::unexpected(attributes.error());
        }
        attribute_list.emplace(std::move(*attributes));
        extended_startup.lpAttributeList = attribute_list->get();
        creation_flags |= EXTENDED_STARTUPINFO_PRESENT;
        startup = &extended_startup.StartupInfo;
    }

    const auto instrumented_process = classify_instrumented_process(spec);
    std::optional<mqb::performance::ScopedWork> execution_evidence;
    if (mqb::performance::current_collector() != nullptr && instrumented_process) {
        execution_evidence.emplace(instrumented_process->work_kind);
    }

    if (spec.cancellation.stop_requested()) return cancelled_result();

    PROCESS_INFORMATION process_info{};
    const auto launch_started = std::chrono::steady_clock::now();
    const BOOL created = ::CreateProcessW(
        executable.c_str(),
        command_line.data(),
        nullptr,
        nullptr,
        use_explicit_standard_handles ? TRUE : FALSE,
        creation_flags,
        environment_pointer,
        working_directory ? working_directory->c_str() : nullptr,
        startup,
        &process_info);
    const auto launch_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - launch_started);
    if (!created) {
        return std::unexpected(error(
            ProcessErrorCode::launch_failed,
            ::GetLastError(),
            "CreateProcessW failed"));
    }
    if (instrumented_process) {
        mqb::performance::record_process_launch(
            instrumented_process->process_kind);
    }

    UniqueHandle process_handle{process_info.hProcess};
    UniqueHandle thread_handle{process_info.hThread};

    if (stdout_pipe) {
        stdout_pipe->write.reset();
    }
    if (stderr_pipe) {
        stderr_pipe->write.reset();
    }
    child_stdin.reset();
    child_stdout.reset();
    child_stderr.reset();
    thread_handle.reset();

    process::ProcessResult result;
    result.launch_duration = launch_duration;
    std::optional<DWORD> stdout_error;
    std::optional<DWORD> stderr_error;
    std::optional<std::thread> stdout_reader;
    std::optional<std::thread> stderr_reader;

    const auto join_readers = [&] {
        if (stdout_reader && stdout_reader->joinable()) stdout_reader->join();
        if (stderr_reader && stderr_reader->joinable()) stderr_reader->join();
    };
    const auto abort_processes = [&]() noexcept {
        if (process_job.valid()) {
            ::TerminateJobObject(process_job.get(), ERROR_OPERATION_ABORTED);
            // Last-handle close is also the fail-closed fallback if termination
            // or accounting failed; never report a successful cancellation then.
            process_job.reset();
        } else {
            ::TerminateProcess(process_handle.get(), ERROR_OPERATION_ABORTED);
        }
        ::WaitForSingleObject(process_handle.get(), INFINITE);
    };
    try {
        if (stdout_pipe) {
            stdout_reader.emplace([&] {
                read_pipe(stdout_pipe->read.get(), result.stdout_text, stdout_error);
            });
        }
        if (stderr_pipe) {
            stderr_reader.emplace([&] {
                read_pipe(stderr_pipe->read.get(), result.stderr_text, stderr_error);
            });
        }
        DWORD wait_result = WAIT_FAILED;
        bool cancelled = false;
        if (cancellation_event.valid()) {
            const HANDLE wait_handles[]{cancellation_event.get(), process_handle.get()};
            wait_result = ::WaitForMultipleObjects(2, wait_handles, FALSE, INFINITE);
            // Cancellation wins when both are observed ready. A stop arriving after
            // root-completion selection need not change an already completed result.
            cancelled = wait_result == WAIT_OBJECT_0;
        } else {
            wait_result = ::WaitForSingleObject(process_handle.get(), INFINITE);
        }
        if (wait_result == WAIT_FAILED) {
            const DWORD native_code = ::GetLastError();
            abort_processes();
            join_readers();
            return std::unexpected(error(
                ProcessErrorCode::wait_failed, native_code, "process completion wait failed"));
        }

        DWORD exit_code = ERROR_CANCELLED;
        if (!cancelled && !::GetExitCodeProcess(process_handle.get(), &exit_code)) {
            const DWORD native_code = ::GetLastError();
            abort_processes();
            join_readers();
            return std::unexpected(error(
                ProcessErrorCode::wait_failed, native_code, "GetExitCodeProcess failed"));
        }
        if (process_job.valid()) {
            // A stoppable request owns descendants even on normal root exit. End
            // their lifetime before joining readers whose pipe writers they inherited.
            auto drained = terminate_and_drain_job(
                process_job.get(), cancelled ? ERROR_CANCELLED : ERROR_PROCESS_ABORTED);
            if (!drained) {
                const auto failure = drained.error();
                abort_processes();
                join_readers();
                return std::unexpected(failure);
            }
            ::WaitForSingleObject(process_handle.get(), INFINITE);
        }
        join_readers();
        result.termination = cancelled
            ? process::ProcessTermination::cancelled
            : process::ProcessTermination::exited;

        if (stdout_error || stderr_error) {
            return std::unexpected(error(
                ProcessErrorCode::io_failed,
                stdout_error.value_or(stderr_error.value_or(ERROR_READ_FAULT)),
                "failed while reading child process output"));
        }

        result.exit_code = static_cast<int>(exit_code);
        return result;
    } catch (...) {
        // Includes census allocation and second-reader construction failures.
        // Never unwind through a joinable reader while writers remain alive.
        abort_processes();
        join_readers();
        throw;
    }
}

} // namespace mqb::platform::windows
