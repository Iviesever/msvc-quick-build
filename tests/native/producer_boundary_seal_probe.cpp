// A bounded NTFS prerequisite experiment, not an MQB producer implementation.
// Build as a standalone Windows C++20 console executable. Pass exactly one
// absolute, nonexistent fixture-root path whose parent already exists.
//
// P-A distinguishes RWH/old-metadata-handle coexistence from a verified mutation
// completed while that RWH remains pending. Only the latter disqualifies the
// proposed leaf gate. P-B checks directory ADD_FILE / DELETE_CHILD sharing, with
// TRAVERSE as control; coexistence leaves namespace exclusion inconclusive.
// No ACL/token/service/MSVC qualification, cleanup, retry, or child process.
//
// Primary API contracts:
// https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew
// https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-fsctl_request_oplock
// https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex
// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-zwseteafile
// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-zwqueryeafile

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>
#include <winternl.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr DWORD kBudgetMs = 30000;
constexpr DWORD kMutationObservationMs = 500;
constexpr DWORD kDrainMs = 2000;
constexpr DWORD kAllShares = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
constexpr NTSTATUS kStatusPending = static_cast<NTSTATUS>(0x00000103L);
constexpr ULONGLONG kPreTime = 132537600000000000ULL;
constexpr ULONGLONG kPostTime = kPreTime + 10000000ULL;
constexpr char kEaName[] = "MQB.SEAL.PROBE";
const ULONGLONG gStarted = GetTickCount64();

using Field = std::pair<std::string_view, std::string>;

std::string number(const ULONGLONG value) { return std::to_string(value); }
std::string boolean(const bool value) { return value ? "true" : "false"; }
std::string handle_number(const HANDLE value) {
    return number(static_cast<ULONGLONG>(reinterpret_cast<std::uintptr_t>(value)));
}

std::string quoted(const std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    std::string result{"\""};
    for (const char character : value) {
        const auto c = static_cast<unsigned char>(character);
        if (c == '\\' || c == '"') { result += '\\'; result += static_cast<char>(c); }
        else if (c < 0x20) {
            result += "\\u00";
            result += hex[c >> 4];
            result += hex[c & 15];
        } else { result += static_cast<char>(c); }
    }
    result += '"';
    return result;
}

std::string utf8(const std::wstring& value) {
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return "<path-conversion-failed>";
    std::string result(static_cast<std::size_t>(length), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), length, nullptr, nullptr) != length)
        return "<path-conversion-failed>";
    return result;
}

// Only the controller writes records. The watchdog terminates with exit 124 if
// any API (including logging or cancellation) exceeds the total process budget.
void record(const std::string_view kind, const std::string_view test,
    const std::string_view call, const std::initializer_list<Field> fields = {}) {
    std::string line = "{\"kind\":" + quoted(kind) + ",\"case\":" + quoted(test)
        + ",\"call\":" + quoted(call) + ",\"elapsed_ms\":"
        + number(GetTickCount64() - gStarted);
    for (const auto& field : fields) line += "," + quoted(field.first) + ":" + field.second;
    line += "}\n";
    DWORD written = 0;
    const BOOL ok = WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line.data(),
        static_cast<DWORD>(line.size()), &written, nullptr);
    if (!ok || written != line.size()) {
        TerminateProcess(GetCurrentProcess(), 125);
        ExitProcess(125);
    }
}

[[noreturn]] void fatal(const std::string_view reason) {
    record("fatal", "controller", "terminate_self", {{"reason", quoted(reason)},
        {"classification", quoted("INCONCLUSIVE")}, {"exit_code", number(124)}});
    // Never unwind storage still owned by a pending kernel I/O or live worker.
    TerminateProcess(GetCurrentProcess(), 124);
    ExitProcess(124);
}

DWORD remaining(const DWORD maximum) {
    const ULONGLONG elapsed = GetTickCount64() - gStarted;
    if (elapsed >= kBudgetMs) fatal("total_budget_exhausted");
    const DWORD left = kBudgetMs - static_cast<DWORD>(elapsed);
    return left < maximum ? left : maximum;
}

class Handle {
public:
    Handle() = default;
    explicit Handle(const HANDLE handle) : value_(handle) {}
    ~Handle() { close(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(std::exchange(other.value_, INVALID_HANDLE_VALUE)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) { close(); value_ = std::exchange(other.value_, INVALID_HANDLE_VALUE); }
        return *this;
    }
    HANDLE get() const { return value_; }
    explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
    DWORD close(const bool emit_record = true) {
        if (!*this) return ERROR_SUCCESS;
        const HANDLE value = value_;
        const BOOL ok = CloseHandle(value_);
        const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
        value_ = INVALID_HANDLE_VALUE;
        if (emit_record) record("call", "controller", "CloseHandle", {{"handle", handle_number(value)},
            {"return", boolean(ok != FALSE)}, {"error", number(error)}});
        if (!ok) fatal("owned_handle_close_failed");
        return error;
    }
private:
    HANDLE value_{INVALID_HANDLE_VALUE};
};

class Watchdog {
public:
    Watchdog() : stop_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
        if (!stop_) fatal("watchdog_event_creation_failed");
        thread_ = Handle(CreateThread(nullptr, 0, run, stop_.get(), 0, nullptr));
        if (!thread_) fatal("watchdog_thread_creation_failed");
    }
    ~Watchdog() {
        SetEvent(stop_.get());
        if (WaitForSingleObject(thread_.get(), kDrainMs) != WAIT_OBJECT_0)
            fatal("watchdog_join_failed");
        // The final successful record stays the summary. These two controller
        // synchronization handles are still checked, but their close is quiet.
        thread_.close(false);
        stop_.close(false);
    }
private:
    static DWORD WINAPI run(void* argument) {
        const ULONGLONG elapsed = GetTickCount64() - gStarted;
        const DWORD timeout = elapsed < kBudgetMs ? kBudgetMs - static_cast<DWORD>(elapsed) : 0;
        if (WaitForSingleObject(static_cast<HANDLE>(argument), timeout) != WAIT_OBJECT_0) {
            // No logging lock or I/O can delay the deadline. The runner must
            // treat exit 124 / absence of the final summary as inconclusive.
            TerminateProcess(GetCurrentProcess(), 124);
        }
        return 0;
    }
    Handle stop_;
    Handle thread_;
};

struct Result {
    bool baseline{false};
    bool inconclusive{false};
    std::vector<std::string> disqualifiers;
    void unknown(const std::string_view test, const std::string_view reason) {
        inconclusive = true;
        record("case_result", test, "classify", {{"result", quoted("INCONCLUSIVE")},
            {"reason", quoted(reason)}});
    }
    void disqualify(const std::string_view test, const std::string_view reason) {
        disqualifiers.emplace_back(reason);
        record("case_result", test, "classify", {{"result", quoted("CANDIDATE_DISQUALIFIED")},
            {"disqualifier", quoted(reason)}});
    }
};

Handle open_file(const std::string_view test, const std::wstring& path,
    const DWORD access, const DWORD share, const DWORD disposition, const DWORD flags,
    DWORD& error) {
    const HANDLE value = CreateFileW(path.c_str(), access, share, nullptr,
        disposition, flags, nullptr);
    error = value == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    record("call", test, "CreateFileW", {{"path", quoted(utf8(path))},
        {"desired_access", number(access)}, {"share_mode", number(share)},
        {"creation_disposition", number(disposition)}, {"flags", number(flags)},
        {"handle", handle_number(value)}, {"error", number(error)}});
    return Handle(value);
}

class Oplock {
public:
    Oplock(const std::string_view test, Handle pin) : test_(test), pin_(std::move(pin)),
        event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
        if (!event_) {
            error_ = GetLastError();
            record("call", test_, "CreateEventW", {{"error", number(error_)}});
            return;
        }
        overlapped_.hEvent = event_.get();
        input_.StructureVersion = REQUEST_OPLOCK_CURRENT_VERSION;
        input_.StructureLength = static_cast<WORD>(sizeof(input_));
        input_.RequestedOplockLevel = OPLOCK_LEVEL_CACHE_READ
            | OPLOCK_LEVEL_CACHE_WRITE | OPLOCK_LEVEL_CACHE_HANDLE;
        input_.Flags = REQUEST_OPLOCK_INPUT_FLAG_REQUEST;
    }
    void request() {
        if (!event_) return;
        // Issue only after construction: should record allocation throw, the
        // fully constructed owner's destructor still cancels/drains this IRP.
        const BOOL ok = DeviceIoControl(pin_.get(), FSCTL_REQUEST_OPLOCK,
            &input_, static_cast<DWORD>(sizeof(input_)), &output_,
            static_cast<DWORD>(sizeof(output_)), nullptr, &overlapped_);
        error_ = ok ? ERROR_SUCCESS : GetLastError();
        granted_ = !ok && error_ == ERROR_IO_PENDING;
        outstanding_ = granted_;
        record("call", test_, "FSCTL_REQUEST_OPLOCK", {{"handle", handle_number(pin_.get())},
            {"event_handle", handle_number(event_.get())}, {"return", boolean(ok != FALSE)},
            {"error", number(error_)}, {"requested_level", number(input_.RequestedOplockLevel)},
            {"request_flags", number(input_.Flags)}, {"granted", boolean(granted_)}});
        if (!outstanding_) {
            record("oplock_return_buffer", test_, "FSCTL_REQUEST_OPLOCK", {
                {"structure_version", number(output_.StructureVersion)},
                {"structure_length", number(output_.StructureLength)},
                {"original_level", number(output_.OriginalOplockLevel)},
                {"new_level", number(output_.NewOplockLevel)}, {"flags", number(output_.Flags)}});
        }
    }
    ~Oplock() { release(); }
    Oplock(const Oplock&) = delete;
    Oplock& operator=(const Oplock&) = delete;
    bool granted() const { return granted_; }
    bool refused() const {
        return error_ == ERROR_OPLOCK_NOT_GRANTED || error_ == ERROR_CANNOT_GRANT_REQUESTED_OPLOCK;
    }
    HANDLE event() const { return event_.get(); }
    bool pending(const std::string_view phase) {
        if (!outstanding_) return false;
        DWORD bytes = 0;
        const BOOL ok = GetOverlappedResult(pin_.get(), &overlapped_, &bytes, FALSE);
        const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
        const bool still_pending = !ok && error == ERROR_IO_INCOMPLETE;
        if (!still_pending) outstanding_ = false;
        record("call", test_, "GetOverlappedResult", {{"phase", quoted(phase)},
            {"handle", handle_number(pin_.get())}, {"return", boolean(ok != FALSE)},
            {"error", number(error)}, {"bytes", number(bytes)}, {"pending", boolean(still_pending)}});
        if (!still_pending) report_completion();
        return still_pending;
    }
    void release() {
        if (!pin_) return;
        if (outstanding_) {
            const BOOL cancelled = CancelIoEx(pin_.get(), &overlapped_);
            const DWORD error = cancelled ? ERROR_SUCCESS : GetLastError();
            record("call", test_, "CancelIoEx", {{"handle", handle_number(pin_.get())},
                {"return", boolean(cancelled != FALSE)}, {"error", number(error)}});
            // ERROR_NOT_FOUND can mean the request completed concurrently. It
            // never authorizes early destruction of OVERLAPPED/output/event.
            const DWORD waited = WaitForSingleObject(event_.get(), remaining(kDrainMs));
            record("call", test_, "WaitForSingleObject", {{"phase", quoted("cancel_drain")},
                {"wait_result", number(waited)}});
            if (waited != WAIT_OBJECT_0) fatal("oplock_cancel_drain_timeout_or_failure");
            if (pending("cancel_drain")) fatal("oplock_event_signalled_but_io_incomplete");
        }
        // Closing the requesting handle also resolves any ACK_REQUIRED break.
        // All overlapped storage remains alive until after completion and close.
        const HANDLE value = pin_.get();
        const DWORD close_error = pin_.close();
        record("call", test_, "CloseHandle", {{"role", quoted("final_pin")},
            {"handle", handle_number(value)}, {"error", number(close_error)}});
        if (close_error != ERROR_SUCCESS) fatal("final_pin_close_failed");
    }
private:
    void report_completion() const {
        record("oplock_completion", test_, "FSCTL_REQUEST_OPLOCK", {
            {"ntstatus", number(static_cast<ULONG>(overlapped_.Internal))},
            {"structure_version", number(output_.StructureVersion)},
            {"structure_length", number(output_.StructureLength)},
            {"original_level", number(output_.OriginalOplockLevel)},
            {"new_level", number(output_.NewOplockLevel)}, {"flags", number(output_.Flags)},
            {"access_mode", number(output_.AccessMode)}, {"share_mode", number(output_.ShareMode)}});
    }
    std::string_view test_;
    Handle pin_;
    Handle event_;
    REQUEST_OPLOCK_INPUT_BUFFER input_{};
    REQUEST_OPLOCK_OUTPUT_BUFFER output_{};
    OVERLAPPED overlapped_{};
    DWORD error_{ERROR_SUCCESS};
    bool granted_{false};
    bool outstanding_{false};
};

using NtSetEaFileFn = NTSTATUS(NTAPI*)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG);
using NtQueryEaFileFn = NTSTATUS(NTAPI*)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG,
    BOOLEAN, PVOID, ULONG, PULONG, BOOLEAN);

template<class Function> Function native_function(const HMODULE module, const char* name) {
    const FARPROC address = module ? GetProcAddress(module, name) : nullptr;
    Function function{};
    static_assert(sizeof(function) == sizeof(address));
    std::memcpy(&function, &address, sizeof(function));
    return function;
}

struct EaHeader { ULONG next; UCHAR flags; UCHAR name_length; USHORT value_length; };
static_assert(sizeof(EaHeader) == 8);

struct EaApis {
    const HMODULE module{GetModuleHandleW(L"ntdll.dll")};
    const NtSetEaFileFn set{native_function<NtSetEaFileFn>(module, "NtSetEaFile")};
    const NtQueryEaFileFn query{native_function<NtQueryEaFileFn>(module, "NtQueryEaFile")};
};

struct Mutation {
    HANDLE old_handle{INVALID_HANDLE_VALUE};
    NtSetEaFileFn set_ea{};
    bool ea{false};
    ULONGLONG value{0};
    BOOL ok{FALSE};
    DWORD error{ERROR_SUCCESS};
    NTSTATUS call_status{0};
    IO_STATUS_BLOCK io{};
    DWORD native_wait{WAIT_OBJECT_0};
    ULONGLONG started_ms{0};
    ULONGLONG completed_ms{0};

    static DWORD WINAPI run(void* argument) {
        auto& mutation = *static_cast<Mutation*>(argument);
        mutation.started_ms = GetTickCount64() - gStarted;
        if (!mutation.ea) {
            const FILETIME time{static_cast<DWORD>(mutation.value),
                static_cast<DWORD>(mutation.value >> 32)};
            mutation.ok = SetFileTime(mutation.old_handle, nullptr, nullptr, &time);
            mutation.error = mutation.ok ? ERROR_SUCCESS : GetLastError();
        } else {
            alignas(ULONG) std::array<unsigned char, 64> buffer{};
            const EaHeader header{0, 0, static_cast<UCHAR>(sizeof(kEaName) - 1), 1};
            std::memcpy(buffer.data(), &header, sizeof(header));
            std::memcpy(buffer.data() + sizeof(header), kEaName, sizeof(kEaName));
            buffer[sizeof(header) + sizeof(kEaName)] = static_cast<unsigned char>(mutation.value);
            mutation.call_status = mutation.set_ea(mutation.old_handle, &mutation.io,
                buffer.data(), static_cast<ULONG>(sizeof(header) + sizeof(kEaName) + 1));
            if (mutation.call_status == kStatusPending) {
                // The old handle is synchronous; retain even this unexpected
                // pending call's buffer and IO_STATUS_BLOCK until completion.
                mutation.native_wait = WaitForSingleObject(mutation.old_handle, kBudgetMs);
                if (mutation.native_wait != WAIT_OBJECT_0) {
                    TerminateProcess(GetCurrentProcess(), 124);
                    ExitProcess(124);
                }
            }
            const NTSTATUS final_status = mutation.call_status == kStatusPending
                ? mutation.io.Status : mutation.call_status;
            mutation.ok = final_status == 0 ? TRUE : FALSE;
        }
        mutation.completed_ms = GetTickCount64() - gStarted;
        return 0;
    }
};

class MutationWorker {
public:
    MutationWorker(Mutation& mutation, Oplock& gate) : gate_(gate),
        thread_(CreateThread(nullptr, 0, Mutation::run, &mutation, 0, nullptr)) {
        error_ = thread_ ? ERROR_SUCCESS : GetLastError();
    }
    ~MutationWorker() { join(); }
    MutationWorker(const MutationWorker&) = delete;
    MutationWorker& operator=(const MutationWorker&) = delete;
    explicit operator bool() const { return static_cast<bool>(thread_); }
    HANDLE get() const { return thread_.get(); }
    DWORD error() const { return error_; }
    void join() {
        if (!thread_) return;
        // Also covers unwinding: release a possible ACK wait and join before
        // the caller's Mutation or its old handle can leave scope.
        gate_.release();
        if (WaitForSingleObject(thread_.get(), remaining(kDrainMs)) != WAIT_OBJECT_0)
            fatal("mutation_worker_did_not_finish_after_gate_release");
        thread_.close();
    }
private:
    Oplock& gate_;
    Handle thread_;
    DWORD error_{ERROR_SUCCESS};
};

void report_mutation(const std::string_view test, const std::string_view phase,
    const Mutation& mutation) {
    record("call", test, mutation.ea ? "NtSetEaFile" : "SetFileTime", {
        {"phase", quoted(phase)}, {"handle", handle_number(mutation.old_handle)},
        {"requested_value", number(mutation.value)}, {"success", boolean(mutation.ok != FALSE)},
        {"win32_error", number(mutation.error)},
        {"ntstatus", number(static_cast<ULONG>(mutation.call_status))},
        {"io_status", number(static_cast<ULONG>(mutation.io.Status))},
        {"io_information", number(mutation.io.Information)},
        {"native_wait", number(mutation.native_wait)},
        {"started_ms", number(mutation.started_ms)}, {"completed_ms", number(mutation.completed_ms)}});
}

bool verify_mutation(const std::string_view test, const std::wstring& path,
    const bool ea, const ULONGLONG expected, const EaApis& apis) {
    DWORD error = 0;
    auto reader = open_file(test, path, GENERIC_READ, kAllShares, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, error);
    if (!reader) return false;
    if (!ea) {
        FILETIME time{};
        const BOOL ok = GetFileTime(reader.get(), nullptr, nullptr, &time);
        error = ok ? ERROR_SUCCESS : GetLastError();
        const ULONGLONG actual = (static_cast<ULONGLONG>(time.dwHighDateTime) << 32)
            | time.dwLowDateTime;
        record("call", test, "GetFileTime", {{"handle", handle_number(reader.get())},
            {"error", number(error)}, {"expected", number(expected)}, {"actual", number(actual)},
            {"verified", boolean(ok && actual == expected)}});
        return ok && actual == expected;
    }
    alignas(ULONG) std::array<unsigned char, 128> buffer{};
    IO_STATUS_BLOCK io{};
    const NTSTATUS status = apis.query(reader.get(), &io, buffer.data(),
        static_cast<ULONG>(buffer.size()), TRUE, nullptr, 0, nullptr, TRUE);
    if (status == kStatusPending) {
        if (WaitForSingleObject(reader.get(), remaining(kDrainMs)) != WAIT_OBJECT_0)
            fatal("ea_query_drain_timeout_or_failure");
    }
    const NTSTATUS final_status = status == kStatusPending ? io.Status : status;
    EaHeader header{};
    std::memcpy(&header, buffer.data(), sizeof(header));
    const bool layout_valid = io.Information >= sizeof(header) + sizeof(kEaName) + 1
        && io.Information <= buffer.size() && header.name_length == sizeof(kEaName) - 1
        && header.value_length == 1
        && std::memcmp(buffer.data() + sizeof(header), kEaName, sizeof(kEaName)) == 0;
    const unsigned char actual = buffer[sizeof(header) + sizeof(kEaName)];
    const bool verified = final_status == 0 && layout_valid && actual == expected;
    record("call", test, "NtQueryEaFile", {{"handle", handle_number(reader.get())},
        {"ntstatus", number(static_cast<ULONG>(status))},
        {"io_status", number(static_cast<ULONG>(io.Status))}, {"io_information", number(io.Information)},
        {"layout_valid", boolean(layout_valid)}, {"expected", number(expected)},
        {"actual", number(actual)}, {"verified", boolean(verified)}});
    return verified;
}

void baseline(const std::wstring& root, Result& result) {
    constexpr std::string_view test = "P-A-baseline";
    DWORD error = 0;
    auto reader = open_file(test, root + L"\\baseline.bin", GENERIC_READ, FILE_SHARE_READ,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, error);
    if (!reader) { result.unknown(test, "baseline_create_failed"); return; }
    Oplock gate(test, std::move(reader));
    gate.request();
    result.baseline = gate.granted() && gate.pending("baseline_grant_check");
    gate.release();
    if (!result.baseline) result.unknown(test, "baseline_rwh_not_observed_pending");
    else record("case_result", test, "classify", {{"result", quoted("BASELINE_GRANTED")}});
}

void leaf_case(const std::wstring& root, const bool ea, const EaApis& apis, Result& result) {
    const std::string_view test = ea ? "P-A-WRITE_EA" : "P-A-WRITE_ATTRIBUTES";
    const std::wstring path = root + (ea ? L"\\write-ea.bin" : L"\\write-attributes.bin");
    if (ea && (!apis.set || !apis.query)) { result.unknown(test, "native_ea_api_unavailable"); return; }
    DWORD error = 0;
    // This is the very handle returned by CREATE_NEW. It is never upgraded,
    // duplicated, or reopened before either mutation, and cannot be renamed
    // away because it does not share DELETE. No preexisting file is modified.
    auto old = open_file(test, path, ea ? FILE_WRITE_EA : FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, error);
    if (!old) { result.unknown(test, "old_writer_create_failed"); return; }
    Mutation pre;
    pre.old_handle = old.get(); pre.ea = ea; pre.set_ea = apis.set;
    pre.value = ea ? 0x31ULL : kPreTime;
    Mutation::run(&pre);
    report_mutation(test, "pre_gate_control", pre);
    if (!pre.ok || !verify_mutation(test, path, ea, pre.value, apis)) {
        result.unknown(test, "same_old_handle_pre_gate_mutation_not_verified"); return;
    }
    // The pre-gate verification handle has closed. Exactly the old metadata
    // writer remains when this independent data-reading FILE_OBJECT is opened.
    auto reader = open_file(test, path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED | FILE_FLAG_OPEN_REPARSE_POINT, error);
    if (!reader) {
        if (error == ERROR_SHARING_VIOLATION)
            record("case_result", test, "classify", {{"result", quoted("EXCLUDED_BY_READ_PIN")}});
        else result.unknown(test, "final_read_pin_failed_without_sharing_violation");
        return;
    }
    Oplock gate(test, std::move(reader));
    gate.request();
    if (gate.granted()) record("subclaim", test, "unique_open_handle", {
        {"holds", boolean(false)}, {"old_writer_preverified", boolean(true)},
        {"rwh_granted", boolean(true)}, {"composite_disqualified_by_coexistence_alone", boolean(false)}});
    else if (!gate.refused()) result.unknown(test, "rwh_failed_without_recognized_refusal");
    const bool pending_before = gate.pending("before_post_gate_mutation");

    Mutation post;
    post.old_handle = old.get(); post.ea = ea; post.set_ea = apis.set;
    post.value = ea ? 0x32ULL : kPostTime;
    record("mutation_attempt", test, ea ? "NtSetEaFile" : "SetFileTime", {
        {"phase", quoted("post_gate")}, {"handle", handle_number(old.get())},
        {"rwh_granted", boolean(gate.granted())}, {"rwh_pending_before", boolean(pending_before)}});
    MutationWorker worker(post, gate);
    record("call", test, "CreateThread", {{"role", quoted("metadata_mutation")},
        {"handle", handle_number(worker.get())}, {"error", number(worker.error())}});
    if (!worker) { result.unknown(test, "mutation_worker_creation_failed"); return; }
    const HANDLE waits[]{worker.get(), gate.event()};
    const DWORD waited = pending_before
        ? WaitForMultipleObjects(2, waits, FALSE, remaining(kMutationObservationMs))
        : WaitForSingleObject(worker.get(), remaining(kMutationObservationMs));
    const bool completed_before_release = WaitForSingleObject(worker.get(), 0) == WAIT_OBJECT_0;
    const bool pending_after = gate.pending("after_mutation_observation");
    record("observation", test, "post_gate_mutation", {{"wait_result", number(waited)},
        {"completed_before_release", boolean(completed_before_release)},
        {"rwh_still_pending", boolean(pending_after)},
        {"call_succeeded_before_release", boolean(completed_before_release && post.ok)},
        {"writer_handle_still_live", boolean(static_cast<bool>(old))}});
    // A break may wait for ACK. Releasing this test gate lets the attempted
    // mutation finish, but that later finish is never reported as penetration.
    worker.join();
    report_mutation(test, "post_gate", post);
    const bool verified = post.ok && verify_mutation(test, path, ea, post.value, apis);
    const bool penetrated = verified && completed_before_release && pending_before && pending_after;
    record("mutation_result", test, "classify", {{"verified_after_release", boolean(verified)},
        {"verified_mutation_completed_with_rwh_pending", boolean(penetrated)}});
    if (penetrated) result.disqualify(test, ea
        ? "write_ea_mutation_completed_while_final_pin_and_pending_rwh_held"
        : "write_attributes_mutation_completed_while_final_pin_and_pending_rwh_held");
    else if (!post.ok || !verified) result.unknown(test, "post_gate_mutation_not_verified");
    else if (gate.granted()) result.unknown(test, completed_before_release
        ? "metadata_mutation_completed_but_rwh_pending_interval_not_proved"
        : "old_metadata_handle_survived_but_mutation_completed_only_after_gate_release");
    else if (gate.refused())
        record("case_result", test, "classify", {{"result", quoted("EXCLUDED_BY_RWH_REFUSAL")}});
}

void directory_case(const std::wstring& root, const std::wstring& name,
    const std::string_view test, const DWORD access, const bool expect_compatible, Result& result) {
    const std::wstring path = root + L"\\" + name;
    const BOOL created = CreateDirectoryW(path.c_str(), nullptr);
    DWORD error = created ? ERROR_SUCCESS : GetLastError();
    record("call", test, "CreateDirectoryW", {{"path", quoted(utf8(path))}, {"error", number(error)}});
    if (!created) { result.unknown(test, "new_directory_creation_failed"); return; }
    auto old = open_file(test, path, access, FILE_SHARE_READ | FILE_SHARE_WRITE,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, error);
    if (!old) { result.unknown(test, "old_directory_handle_open_failed"); return; }
    constexpr DWORD final_access = FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES;
    auto pin = open_file(test, path, final_access, FILE_SHARE_READ, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, error);
    record("compatibility", test, "directory_parent_pin", {{"old_handle", handle_number(old.get())},
        {"pin_handle", handle_number(pin.get())}, {"old_desired_access", number(access)},
        {"final_desired_access", number(final_access)},
        {"compatible", boolean(static_cast<bool>(pin))}, {"error", number(error)},
        {"directory_mutation_attempted", boolean(false)}});
    if (expect_compatible) {
        if (!pin) result.unknown(test, "directory_traverse_positive_control_failed");
        else record("case_result", test, "classify", {{"result", quoted("TRAVERSE_CONTROL_COMPATIBLE")}});
    } else if (pin) {
        record("subclaim", test, "parent_pin_excludes_old_directory_handle", {
            {"holds", boolean(false)}, {"namespace_mutation_tested", boolean(false)},
            {"acl_revocation_tested", boolean(false)},
            {"composite_disqualified_by_coexistence_alone", boolean(false)}});
        result.unknown(test, access == FILE_ADD_FILE
            ? "directory_add_file_coexists_relative_mutation_and_acl_obligation_pending"
            : "directory_delete_child_coexists_permission_necessity_and_relative_mutation_pending");
    } else if (error != ERROR_SHARING_VIOLATION) {
        result.unknown(test, "directory_pin_failure_not_a_sharing_violation");
    } else {
        record("case_result", test, "classify", {{"result", quoted("EXCLUDED_BY_PARENT_PIN")}});
    }
}

bool prepare_root(const std::wstring& supplied, std::wstring& root, Handle& retained, Result& result) {
    // Deliberately support only ordinary absolute drive paths, with ample room
    // for the fixed leaf names. No UNC/device paths, alternate streams, or .. .
    if (supplied.size() < 4 || supplied.size() > MAX_PATH - 64
        || !((supplied[0] >= L'A' && supplied[0] <= L'Z') || (supplied[0] >= L'a' && supplied[0] <= L'z'))
        || supplied[1] != L':'
        || supplied[2] != L'\\' || supplied.find(L':', 2) != std::wstring::npos
        || supplied.find(L'/') != std::wstring::npos || supplied.back() == L'\\') {
        result.unknown("setup", "ordinary_absolute_new_root_required"); return false;
    }
    for (std::size_t start = 3; start < supplied.size();) {
        const auto end = supplied.find(L'\\', start);
        const auto component = supplied.substr(start, end == std::wstring::npos ? end : end - start);
        if (component.empty() || component == L"." || component == L".."
            || component.back() == L'.' || component.back() == L' ') {
            result.unknown("setup", "ambiguous_path_component"); return false;
        }
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    root = supplied;
    const auto last = root.find_last_of(L'\\');
    const std::wstring parent = last == 2 ? root.substr(0, 3) : root.substr(0, last);
    const DWORD parent_attributes = GetFileAttributesW(parent.c_str());
    const DWORD parent_error = parent_attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
    record("call", "setup", "GetFileAttributesW", {{"path", quoted(utf8(parent))},
        {"attributes", number(parent_attributes)}, {"error", number(parent_error)}});
    if (parent_attributes == INVALID_FILE_ATTRIBUTES || !(parent_attributes & FILE_ATTRIBUTE_DIRECTORY)
        || (parent_attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        result.unknown("setup", "existing_non_reparse_parent_required"); return false;
    }
    // Reject reparse ancestors before any fixture creation, not just the final
    // parent. This is a local primitive fixture, not a path-admission protocol.
    for (std::size_t separator = root.find(L'\\', 3); separator != std::wstring::npos;
        separator = root.find(L'\\', separator + 1)) {
        const std::wstring ancestor = root.substr(0, separator);
        const DWORD attributes = GetFileAttributesW(ancestor.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            result.unknown("setup", "reparse_or_unreadable_ancestor"); return false;
        }
    }
    std::array<wchar_t, MAX_PATH> volume{};
    if (!GetVolumePathNameW(parent.c_str(), volume.data(), static_cast<DWORD>(volume.size()))) {
        record("call", "setup", "GetVolumePathNameW", {{"error", number(GetLastError())}});
        result.unknown("setup", "volume_lookup_failed"); return false;
    }
    std::array<wchar_t, 32> filesystem{};
    DWORD serial = 0, flags = 0;
    const BOOL volume_ok = GetVolumeInformationW(volume.data(), nullptr, 0, &serial, nullptr,
        &flags, filesystem.data(), static_cast<DWORD>(filesystem.size()));
    const DWORD volume_error = volume_ok ? ERROR_SUCCESS : GetLastError();
    const UINT drive_type = GetDriveTypeW(volume.data());
    record("call", "setup", "GetVolumeInformationW", {{"volume", quoted(utf8(volume.data()))},
        {"filesystem", quoted(utf8(filesystem.data()))}, {"error", number(volume_error)},
        {"serial", number(serial)}, {"flags", number(flags)}, {"drive_type", number(drive_type)}});
    if (!volume_ok || drive_type != DRIVE_FIXED || _wcsicmp(filesystem.data(), L"NTFS") != 0) {
        result.unknown("setup", "local_fixed_ntfs_required"); return false;
    }
    const BOOL created = CreateDirectoryW(root.c_str(), nullptr);
    const DWORD create_error = created ? ERROR_SUCCESS : GetLastError();
    record("call", "setup", "CreateDirectoryW", {{"path", quoted(utf8(root))},
        {"error", number(create_error)}, {"created_new", boolean(created != FALSE)}});
    if (!created) { result.unknown("setup", "fixture_root_must_be_new_no_retry"); return false; }
    DWORD error = 0;
    retained = open_file("setup", root, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, error);
    if (!retained) { result.unknown("setup", "fixture_root_retention_failed"); return false; }
    return true;
}

int summary(const Result& result) {
    const bool disqualified = !result.disqualifiers.empty();
    const bool inconclusive = result.inconclusive || !result.baseline;
    const char* classification = disqualified ? "CANDIDATE_DISQUALIFIED"
        : inconclusive ? "INCONCLUSIVE" : "NECESSARY_CHECKS_ONLY";
    const int exit_code = disqualified ? 2 : inconclusive ? 3 : 0;
    std::string reasons{"["};
    for (const auto& reason : result.disqualifiers) {
        if (reasons.size() > 1) reasons += ',';
        reasons += quoted(reason);
    }
    reasons += ']';
    record("summary", "controller", "classify", {{"classification", quoted(classification)},
        {"exit_code", number(static_cast<ULONGLONG>(exit_code))},
        {"baseline_rwh_granted", boolean(result.baseline)}, {"has_inconclusive_case", boolean(inconclusive)},
        {"disqualifiers", reasons}, {"A1", boolean(false)}, {"A2", boolean(false)}, {"A4", boolean(false)},
        {"service_qualification", boolean(false)}, {"msvc_qualification", boolean(false)},
        {"conditional_acl_qualification", boolean(false)}, {"child_processes_created", number(0)},
        {"file_payload_bytes_created", number(0)}, {"fixture_preserved", boolean(true)},
        {"scope", quoted("same_process_independent_file_objects_necessary_primitives_only")}});
    return exit_code;
}

} // namespace

int wmain(const int argc, wchar_t* argv[]) {
    Watchdog watchdog;
    Result result;
    record("start", "controller", "probe", {{"schema", number(1)}, {"budget_ms", number(kBudgetMs)},
        {"pid", number(GetCurrentProcessId())}, {"child_process_budget", number(0)},
        {"maximum_auxiliary_threads", number(2)}, {"watchdog_exit_code", number(124)},
        {"retries", number(0)}, {"fixture_cleanup", boolean(false)}});
    if (argc != 2) {
        result.unknown("setup", "usage_probe_exe_absolute_nonexistent_fixture_root");
        return summary(result);
    }
    std::wstring root;
    Handle retained_root;
    if (!prepare_root(argv[1], root, retained_root, result)) return summary(result);
    const EaApis apis;
    record("call", "setup", "GetProcAddress", {{"module", quoted("ntdll.dll")},
        {"NtSetEaFile_available", boolean(apis.set != nullptr)},
        {"NtQueryEaFile_available", boolean(apis.query != nullptr)}});
    baseline(root, result);
    leaf_case(root, false, apis, result);
    leaf_case(root, true, apis, result);
    directory_case(root, L"add-file", "P-B-ADD_FILE", FILE_ADD_FILE, false, result);
    directory_case(root, L"delete-child", "P-B-DELETE_CHILD", FILE_DELETE_CHILD, false, result);
    directory_case(root, L"traverse", "P-B-TRAVERSE", FILE_TRAVERSE, true, result);
    retained_root.close();
    return summary(result);
}
