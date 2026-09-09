// Diagnostic fixture only. Never called by the production CLI.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <cstdint>
#include <climits>
#include <utility>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <set>
#include <tuple>
#include <string>
#include <string_view>
#include <vector>
#include "mqb/platform/windows/CommandLine.hpp"

namespace {
namespace fs = std::filesystem;
constexpr GUID file_provider{0xedd08927,0x9cc4,0x4e65,{0xb9,0x70,0xc2,0x56,0x0f,0xb5,0xc2,0x89}};
constexpr GUID process_provider{0x22fb2cd6,0x0e7b,0x422b,{0xa0,0xc7,0x2f,0xad,0x1f,0xd0,0xe7,0x16}};
void require(bool ok, const std::string& text) { if (!ok) throw std::runtime_error(text); }
void checked(ULONG code, const char* call) {
    require(code == ERROR_SUCCESS, std::string{call} + " native=" + std::to_string(code));
}
std::string utf8(std::wstring_view text) {
    auto result = mqb::platform::windows::utf16_to_utf8(text);
    require(result.has_value(), "invalid UTF-16 in trace metadata");
    return *result;
}
std::string js(std::string_view text) {
    std::string out = "\"";
    constexpr char h[] = "0123456789abcdef";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 32) { out += "\\u00"; out += h[c >> 4]; out += h[c & 15]; }
        else out += static_cast<char>(c);
    }
    return out + '"';
}
std::string hex(const void* memory, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(memory);
    constexpr char h[] = "0123456789abcdef";
    std::string out; out.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) { out += h[bytes[i] >> 4]; out += h[bytes[i] & 15]; }
    return out;
}
void save(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary); out << text; out.flush();
    require(bool(out), "cannot save " + utf8(path.wstring()));
}
std::uint64_t qpc() { LARGE_INTEGER n{}; require(::QueryPerformanceCounter(&n), "QPC failed"); return n.QuadPart; }
std::uint64_t filetime(FILETIME t) { return (std::uint64_t{t.dwHighDateTime} << 32) | t.dwLowDateTime; }
struct Handle {
    HANDLE value{};
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) ::CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};
struct Buffer {
    std::vector<std::uint64_t> words;
    explicit Buffer(ULONG bytes) : words((bytes + 7ULL) / 8) {}
    template<class T> T* as() { return reinterpret_cast<T*>(words.data()); }
};
std::wstring bounded_string(void* data, ULONG bytes, ULONG offset) {
    require(offset < bytes && offset % 2 == 0, "bad metadata string offset");
    const auto* start = reinterpret_cast<const wchar_t*>(static_cast<char*>(data) + offset);
    auto count = (bytes - offset) / sizeof(wchar_t);
    auto end = std::find(start, start + count, L'\0');
    require(end != start + count, "unterminated metadata string");
    return std::wstring{start, end};
}
// Restore the previous token privilege state, even on a failed capture.
struct ProfilePrivilege {
    Handle token;
    TOKEN_PRIVILEGES previous{};
    bool changed{};
    ProfilePrivilege() : token([] { HANDLE h{}; require(::OpenProcessToken(::GetCurrentProcess(),
        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &h), "OpenProcessToken failed"); return h; }()) {
        TOKEN_PRIVILEGES request{}; request.PrivilegeCount = 1;
        require(::LookupPrivilegeValueW(nullptr, L"SeSystemProfilePrivilege", &request.Privileges[0].Luid), "privilege lookup failed");
        request.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        DWORD bytes = sizeof(previous); ::SetLastError(ERROR_SUCCESS);
        require(::AdjustTokenPrivileges(token.value, FALSE, &request, sizeof(previous), &previous, &bytes), "privilege adjustment failed");
        changed = true; checked(::GetLastError(), "AdjustTokenPrivileges");
    }
    ~ProfilePrivilege() { if (changed) ::AdjustTokenPrivileges(token.value, FALSE, &previous, 0, nullptr, nullptr); }
};
ULONGLONG keyword_mask(const GUID& provider, const fs::path& output,
                       const std::vector<std::wstring>& required_names) {
    ULONG bytes{}; auto guid = provider;
    checked(::TdhEnumerateProviderFieldInformation(&guid, EventKeywordInformation, nullptr, &bytes) == ERROR_INSUFFICIENT_BUFFER
        ? ERROR_SUCCESS : ERROR_INVALID_DATA, "TDH keyword size");
    require(bytes && bytes <= 1024 * 1024, "unexpected keyword metadata size");
    Buffer storage(bytes); auto* list = storage.as<PROVIDER_FIELD_INFOARRAY>();
    checked(::TdhEnumerateProviderFieldInformation(&guid, EventKeywordInformation, list, &bytes), "TDH keywords");
    ULONGLONG mask{}; std::vector<bool> found(required_names.size());
    std::string report = "[";
    for (ULONG i = 0; i < list->NumberOfElements; ++i) {
        const auto& field = list->FieldInfoArray[i];
        auto name = bounded_string(list, bytes, field.NameOffset);
        if (i) report += ',';
        report += "{\"name\":" + js(utf8(name)) + ",\"value\":" + std::to_string(field.Value) + "}";
        for (std::size_t j = 0; j < required_names.size(); ++j) {
            if (name.ends_with(required_names[j])) { mask |= field.Value; found[j] = true; }
        }
    }
    save(output, report + "]\n");
    require(std::all_of(found.begin(), found.end(), [](bool value) { return value; }), "required ETW keyword unavailable; see metadata");
    return mask;
}
struct Session {
    TRACEHANDLE handle{};
    Buffer buffer;
    std::wstring name;
    bool started{};
    ULONG stop_status{ERROR_INVALID_STATE};
    Session(const fs::path& file, std::wstring session_name)
        : buffer(static_cast<ULONG>(sizeof(EVENT_TRACE_PROPERTIES) + (file.wstring().size() + session_name.size() + 2) * sizeof(wchar_t))),
          name(std::move(session_name)) {
        auto* p = properties();
        p->Wnode.BufferSize = static_cast<ULONG>(buffer.words.size() * sizeof(std::uint64_t));
        p->Wnode.Flags = WNODE_FLAG_TRACED_GUID; p->Wnode.ClientContext = 1; // QPC timestamps.
        p->BufferSize = 256; p->MinimumBuffers = 32; p->MaximumBuffers = 128;
        p->LogFileMode = EVENT_TRACE_FILE_MODE_SEQUENTIAL | EVENT_TRACE_REAL_TIME_MODE; p->MaximumFileSize = 256; p->FlushTimer = 1;
        p->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
        p->LogFileNameOffset = p->LoggerNameOffset + static_cast<ULONG>((name.size() + 1) * sizeof(wchar_t));
        auto* base = reinterpret_cast<char*>(p);
        std::memcpy(base + p->LoggerNameOffset, name.c_str(), (name.size() + 1) * sizeof(wchar_t));
        const auto path = file.wstring(); std::memcpy(base + p->LogFileNameOffset, path.c_str(), (path.size() + 1) * sizeof(wchar_t));
        checked(::StartTraceW(&handle, name.c_str(), p), "StartTraceW");
        started = true; // A name collision is refused, never stopped or reused.
    }
    EVENT_TRACE_PROPERTIES* properties() { return buffer.as<EVENT_TRACE_PROPERTIES>(); }
    void enable(const GUID& guid, ULONGLONG mask) {
        ENABLE_TRACE_PARAMETERS parameters{}; parameters.Version = ENABLE_TRACE_PARAMETERS_VERSION_2;
        checked(::EnableTraceEx2(handle, &guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_VERBOSE,
                                mask, 0, 10000, &parameters), "EnableTraceEx2");
    }
    void stop() {
        if (!started) return;
        stop_status = ::ControlTraceW(handle, name.c_str(), properties(), EVENT_TRACE_CONTROL_STOP);
        if (stop_status == ERROR_SUCCESS) started = false;
    }
    ~Session() { if (started) stop(); }
};
std::string process_identity() {
    FILETIME created{}, end{}, kernel{}, user{};
    require(::GetProcessTimes(::GetCurrentProcess(), &created, &end, &kernel, &user), "process identity failed");
    return "{\"pid\":" + std::to_string(::GetCurrentProcessId()) + ",\"tid\":" + std::to_string(::GetCurrentThreadId())
        + ",\"created_filetime\":" + std::to_string(filetime(created)) + "}";
}
struct CalibrationWindow {
    std::uint64_t start{}, denied_before{}, denied_after{};
    DWORD pid{}, tid{};
};
std::string calibration(const fs::path& path, CalibrationWindow* window = nullptr) {
    const auto start = qpc();
    Handle owner{::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                              nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
    require(bool(owner), "cannot create unique calibration file native=" + std::to_string(::GetLastError()));
    FILE_ID_INFO id{};
    require(::GetFileInformationByHandleEx(owner.value, FileIdInfo, &id, sizeof(id)), "calibration file identity failed");
    const auto before = qpc();
    Handle denied{::CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    const DWORD error = denied ? ERROR_SUCCESS : ::GetLastError();
    const auto after = qpc();
    require(!denied && error == ERROR_SHARING_VIOLATION, "controlled sharing conflict missing native=" + std::to_string(error));
    if (window) *window = {start, before, after, ::GetCurrentProcessId(), ::GetCurrentThreadId()};
    return "{\"path\":" + js(utf8(path.wstring())) + ",\"owner\":" + process_identity()
        + ",\"start_qpc\":" + std::to_string(start) + ",\"denied_before_qpc\":" + std::to_string(before)
        + ",\"denied_after_qpc\":" + std::to_string(after) + ",\"win32_error\":" + std::to_string(error)
        + ",\"expected_ntstatus\":3221225539,\"volume\":" + std::to_string(id.VolumeSerialNumber)
        + ",\"file_id\":" + js(hex(id.FileId.Identifier, sizeof(id.FileId.Identifier))) + "}";
}
// TDH decodes selected named properties from the host's actual event schema.
// Raw payload/version/flags are retained; no guessed hard-coded byte offsets.
struct FileFields {
    std::optional<std::uint64_t> irp, status, issuing_tid;
    std::wstring path;
};
std::string event_data(EVENT_RECORD* event, std::string* schema, FileFields* fields = nullptr) {
    ULONG bytes{};
    auto status = ::TdhGetEventInformation(event, 0, nullptr, nullptr, &bytes);
    require(status == ERROR_INSUFFICIENT_BUFFER && bytes <= 1024 * 1024, "TDH event metadata unavailable");
    Buffer storage(bytes); auto* info = storage.as<TRACE_EVENT_INFO>();
    checked(::TdhGetEventInformation(event, 0, nullptr, info, &bytes), "TdhGetEventInformation");
    const std::vector<std::wstring> selected{L"Irp", L"FileObject", L"FileName", L"Status", L"NtStatus", L"IssuingThreadId", L"ThreadId",
        L"CreateOptions", L"CreateAttributes", L"ShareAccess", L"ProcessID", L"ThreadID", L"CreateTime", L"ImageName", L"ParentProcessID"};
    if (schema) {
        *schema = "[";
        for (ULONG i = 0; i < info->TopLevelPropertyCount; ++i) {
            const auto& p = info->EventPropertyInfoArray[i];
            if (i) *schema += ',';
            *schema += "{\"name\":" + js(utf8(bounded_string(info, bytes, p.NameOffset)))
                + ",\"flags\":" + std::to_string(p.Flags)
                + ",\"in_type\":" + ((p.Flags & PropertyStruct) ? "null" : std::to_string(p.nonStructType.InType)) + "}";
        }
        *schema += ']';
    }
    std::string out = "{"; bool first = true;
    for (ULONG i = 0; i < info->TopLevelPropertyCount; ++i) {
        const auto& property = info->EventPropertyInfoArray[i];
        auto name = bounded_string(info, bytes, property.NameOffset);
        if (std::find(selected.begin(), selected.end(), name) == selected.end()) continue;
        require(!(property.Flags & PropertyStruct), "selected ETW property is structured");
        PROPERTY_DATA_DESCRIPTOR descriptor{};
        descriptor.PropertyName = reinterpret_cast<ULONGLONG>(name.c_str()); descriptor.ArrayIndex = ULONG_MAX;
        ULONG size{};
        checked(::TdhGetPropertySize(event, 0, nullptr, 1, &descriptor, &size), "TdhGetPropertySize");
        require(size && size <= 65536, "unexpected ETW property size");
        Buffer value(size); auto* data = value.as<BYTE>();
        checked(::TdhGetProperty(event, 0, nullptr, 1, &descriptor, size, data), "TdhGetProperty");
        std::string encoded;
        const auto type = property.nonStructType.InType;
        if (type == TDH_INTYPE_UNICODESTRING) {
            const auto text = bounded_string(data, size, 0);
            encoded = js(utf8(text));
            if (fields && name == L"FileName") fields->path = text;
        } else if (type == TDH_INTYPE_ANSISTRING) {
            // ProcessStop v2 ImageName is ANSI; retain exact bytes rather than
            // guessing an encoding or replacing the Unicode ProcessStart identity.
            require(data[size - 1] == 0, "unterminated ANSI event property");
            encoded = "{\"encoding\":\"opaque-ansi\",\"bytes\":" + js(hex(data, size - 1)) + "}";
        } else if ((type == TDH_INTYPE_UINT32 || type == TDH_INTYPE_HEXINT32 || type == TDH_INTYPE_UINT64 ||
                    type == TDH_INTYPE_HEXINT64 || type == TDH_INTYPE_POINTER || type == TDH_INTYPE_FILETIME) && (size == 4 || size == 8)) {
            std::uint64_t number{}; std::memcpy(&number, data, size); encoded = std::to_string(number);
            if (fields) {
                if (name == L"Irp") fields->irp = number;
                if (name == L"Status") fields->status = number;
                if (name == L"IssuingThreadId" || name == L"ThreadId") fields->issuing_tid = number;
            }
        } else { throw std::runtime_error("unsupported selected field " + utf8(name) + " type=" + std::to_string(type)); }
        if (!first) out += ','; first = false;
        out += js(utf8(name)) + ':' + encoded;
    }
    return out + '}';
}
struct Decoder {
    std::ofstream output;
    std::uint64_t total{}, selected{}, errors{}, capped{};
    std::set<std::tuple<bool, USHORT, UCHAR>> schemas;
    explicit Decoder(const fs::path& file) : output(file, std::ios::binary) { require(bool(output), "cannot open events output"); }
    static void WINAPI callback(EVENT_RECORD* event) noexcept {
        auto& self = *static_cast<Decoder*>(event->UserContext);
        ++self.total;
        const auto id = event->EventHeader.EventDescriptor.Id;
        const bool is_file = ::IsEqualGUID(event->EventHeader.ProviderId, file_provider);
        const bool is_process = ::IsEqualGUID(event->EventHeader.ProviderId, process_provider);
        if (!(is_file && (id == 12 || id == 24)) && !(is_process && id >= 1 && id <= 4)) return;
        if (++self.selected > 500000) { ++self.capped; return; }
        try {
            std::string decoded, error, schema = "null";
            const bool first_schema = self.schemas.emplace(is_file, id, event->EventHeader.EventDescriptor.Version).second;
            try { decoded = event_data(event, first_schema ? &schema : nullptr); } catch (const std::exception& e) { error = e.what(); ++self.errors; decoded = "null"; }
            self.output << "{\"sequence\":" << self.total << ",\"provider\":" << js(is_file ? "file" : "process")
                << ",\"id\":" << id << ",\"version\":" << unsigned(event->EventHeader.EventDescriptor.Version)
                << ",\"flags\":" << event->EventHeader.Flags << ",\"pid\":" << event->EventHeader.ProcessId
                << ",\"tid\":" << event->EventHeader.ThreadId << ",\"qpc\":" << event->EventHeader.TimeStamp.QuadPart
                << ",\"raw_payload\":" << js(hex(event->UserData, event->UserDataLength))
                << ",\"property_schema\":" << schema
                << ",\"decode_error\":" << (error.empty() ? "null" : js(error)) << ",\"data\":" << decoded << "}\n";
            if (!self.output) ++self.errors;
        } catch (...) { ++self.errors; }
    }
};
// Consume the SAME file+real-time session. No sleep, readiness-marker retry, or
// discarded calibration. The ETL remains authoritative: offline audit must find
// these exact live-observed payloads again, plus both original boundary controls.
struct LiveReadiness {
    struct Event {
        std::uint64_t stamp{}, irp{}, status{}, issuing_tid{};
        DWORD pid{}, tid{};
        std::wstring path;
        std::string json;
    };
    struct Pair {
        Event begin, end;
        std::string json() const { return "{\"begin\":" + begin.json + ",\"end\":" + end.json + "}"; }
    };
    std::mutex mutex;
    std::condition_variable changed;
    std::map<std::uint64_t, Event> pending;
    std::optional<Pair> provider_pair;
    std::vector<Pair> opening_pairs;
    std::wstring name, device, drive, target;
    EVENT_TRACE_LOGFILEW log{};
    TRACEHANDLE trace{INVALID_PROCESSTRACE_HANDLE};
    std::jthread consumer;
    std::chrono::steady_clock::time_point deadline;
    std::uint64_t frequency{}, started_qpc{}, expires_qpc{}, provider_ack{}, opening_ack{}, decision_qpc{};
    std::uint64_t errors{};
    ULONG process_status{ERROR_INVALID_STATE}, close_status{ERROR_INVALID_STATE};
    DWORD wait_status{ERROR_INVALID_STATE};
    bool active{}, admitted{}, done{};
    std::string phase{"not-started"}, error;

    LiveReadiness(std::wstring session, std::wstring device_root, std::wstring dos_root, std::uint64_t hz)
        : name(std::move(session)), device(std::move(device_root)), drive(std::move(dos_root)), frequency(hz) {
        log.LoggerName = name.data();
        log.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_RAW_TIMESTAMP;
        log.EventRecordCallback = callback; log.Context = this;
        trace = ::OpenTraceW(&log);
        require(trace != INVALID_PROCESSTRACE_HANDLE, "OpenTrace live readiness failed native=" + std::to_string(::GetLastError()));
        try {
            consumer = std::jthread([this] {
                const auto result = ::ProcessTrace(&trace, 1, nullptr, nullptr);
                std::lock_guard lock{mutex}; process_status = result; done = true; changed.notify_all();
            });
        } catch (...) { ::CloseTrace(trace); trace = INVALID_PROCESSTRACE_HANDLE; throw; }
    }
    ~LiveReadiness() { finish(); }
    void finish() {
        if (trace == INVALID_PROCESSTRACE_HANDLE) return;
        // ERROR_CTX_CLOSE_PENDING is a documented successful real-time close.
        // Joining can wait for ETW's queued buffers; this is not a hard deadline.
        close_status = ::CloseTrace(trace);
        if (consumer.joinable()) consumer.join();
        trace = INVALID_PROCESSTRACE_HANDLE;
    }
    void start() {
        std::lock_guard lock{mutex};
        require(!active, "readiness may only start once");
        active = true; started_qpc = qpc(); expires_qpc = started_qpc + frequency * 10;
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
        phase = "waiting-provider";
    }
    std::wstring canonical(std::wstring path) const {
        std::replace(path.begin(), path.end(), L'/', L'\\');
        if (path.size() > device.size() && path[device.size()] == L'\\' &&
            ::CompareStringOrdinal(path.data(), static_cast<int>(device.size()), device.data(), static_cast<int>(device.size()), TRUE) == CSTR_EQUAL)
            path = drive + path.substr(device.size());
        if (path.starts_with(L"\\??\\") || path.starts_with(L"\\\\?\\")) path.erase(0, 4);
        return path;
    }
    static void WINAPI callback(EVENT_RECORD* record) noexcept {
        auto& self = *static_cast<LiveReadiness*>(record->UserContext);
        try {
            std::lock_guard lock{self.mutex};
            if (!self.active || self.admitted || self.errors || self.wait_status == WAIT_TIMEOUT) return;
            const auto id = record->EventHeader.EventDescriptor.Id;
            if (!::IsEqualGUID(record->EventHeader.ProviderId, file_provider) || (id != 12 && id != 24)) return;
            FileFields fields;
            const auto data = event_data(record, nullptr, &fields);
            require(fields.irp.has_value(), "live event missing IRP");
            Event event;
            event.stamp = static_cast<std::uint64_t>(record->EventHeader.TimeStamp.QuadPart);
            event.irp = *fields.irp; event.pid = record->EventHeader.ProcessId; event.tid = record->EventHeader.ThreadId;
            event.issuing_tid = fields.issuing_tid.value_or(event.tid);
            event.path = self.canonical(fields.path);
            event.json = "{\"provider\":\"file\",\"id\":" + std::to_string(id)
                + ",\"version\":" + std::to_string(record->EventHeader.EventDescriptor.Version)
                + ",\"pid\":" + std::to_string(event.pid) + ",\"tid\":" + std::to_string(event.tid)
                + ",\"qpc\":" + std::to_string(event.stamp)
                + ",\"raw_payload\":" + js(hex(record->UserData, record->UserDataLength)) + ",\"data\":" + data + "}";
            if (event.stamp < self.started_qpc) return;
            if (id == 12) {
                require(!event.path.empty(), "live Create missing file name");
                require(self.pending.size() < 8192 && !self.pending.contains(event.irp), "live pending IRP ambiguity/cap");
                self.pending.emplace(event.irp, std::move(event));
            } else {
                require(fields.status.has_value() && *fields.status <= 0xffffffffULL, "live end missing native Status");
                event.status = *fields.status;
                const auto it = self.pending.find(event.irp);
                if (it == self.pending.end()) return; // Other operations also emit OperationEnd.
                Pair pair{std::move(it->second), std::move(event)}; self.pending.erase(it);
                require(pair.begin.stamp <= pair.end.stamp, "live completion precedes Create");
                if (!self.provider_pair && pair.end.status == ERROR_SUCCESS) {
                    self.provider_pair = pair; self.provider_ack = qpc();
                }
                if (!self.target.empty() && _wcsicmp(pair.begin.path.c_str(), self.target.c_str()) == 0) {
                    require(self.opening_pairs.size() < 2, "duplicate opening calibration pair");
                    self.opening_pairs.push_back(std::move(pair));
                }
                self.changed.notify_all();
            }
        } catch (const std::exception& e) {
            std::lock_guard lock{self.mutex}; ++self.errors; self.error = e.what(); self.changed.notify_all();
        } catch (...) {
            std::lock_guard lock{self.mutex}; ++self.errors; self.changed.notify_all();
        }
    }
    template<class Predicate> void wait(std::unique_lock<std::mutex>& lock, Predicate satisfied) {
        if (!changed.wait_until(lock, deadline, [&] { return errors || done || satisfied(); }) || qpc() > expires_qpc) {
            wait_status = WAIT_TIMEOUT; decision_qpc = qpc();
            throw std::runtime_error("recording readiness timeout; compiler not admitted");
        }
        require(errors == 0 && !done && satisfied(), "recording readiness unavailable: " + error);
    }
    void wait_provider() {
        std::unique_lock lock{mutex}; wait(lock, [&] { return provider_pair.has_value(); });
    }
    void arm(const fs::path& marker) {
        std::lock_guard lock{mutex};
        require(provider_pair.has_value() && target.empty(), "invalid opening calibration admission");
        target = canonical(marker.wstring()); phase = "waiting-opening-calibration";
    }
    void wait_calibration(const CalibrationWindow& window) {
        std::unique_lock lock{mutex}; wait(lock, [&] { return opening_pairs.size() == 2; });
        unsigned good{}, denied{};
        for (const auto& pair : opening_pairs) {
            require(pair.begin.pid == window.pid && pair.begin.issuing_tid == window.tid, "live marker process/thread mismatch");
            if (pair.end.status == 0 && window.start <= pair.begin.stamp && pair.end.stamp <= window.denied_before) ++good;
            if (pair.end.status == 0xc0000043ULL && window.denied_before <= pair.begin.stamp && pair.end.stamp <= window.denied_after) ++denied;
        }
        require(good == 1 && denied == 1, "live opening calibration status/order mismatch");
        opening_ack = qpc();
        require(opening_ack <= expires_qpc, "opening acknowledgement exceeded readiness deadline");
        wait_status = WAIT_OBJECT_0; decision_qpc = opening_ack; admitted = true; phase = "admitted";
        pending.clear(); // No further live decoding; offline ETL audit checks the entire trace.
    }
    bool healthy() const {
        return admitted && errors == 0 && process_status == ERROR_SUCCESS &&
            (close_status == ERROR_SUCCESS || close_status == ERROR_CTX_CLOSE_PENDING);
    }
    std::string report(bool withheld) const {
        std::string pairs = "[";
        for (const auto& pair : opening_pairs) { if (pairs.size() > 1) pairs += ','; pairs += pair.json(); }
        return "{\"schema\":1,\"mode\":\"same-session-real-time\",\"wait_limit_ms\":10000,\"pending_irp_limit\":8192"
            ",\"file_provider_withheld\":" + std::string{withheld ? "true" : "false"}
            + ",\"accepted\":" + (admitted ? "true" : "false") + ",\"phase\":" + js(phase)
            + ",\"start_qpc\":" + std::to_string(started_qpc) + ",\"deadline_qpc\":" + std::to_string(expires_qpc)
            + ",\"provider_ack_qpc\":" + (provider_pair ? std::to_string(provider_ack) : "null")
            + ",\"opening_ack_qpc\":" + (admitted ? std::to_string(opening_ack) : "null")
            + ",\"decision_qpc\":" + std::to_string(decision_qpc)
            + ",\"wait_status\":" + std::to_string(wait_status) + ",\"decode_errors\":" + std::to_string(errors)
            + ",\"error\":" + (error.empty() ? "null" : js(error))
            + ",\"process_trace_status\":" + std::to_string(process_status) + ",\"close_trace_status\":" + std::to_string(close_status)
            + ",\"provider_pair\":" + (provider_pair ? provider_pair->json() : "null")
            + ",\"opening_pairs\":" + pairs + "]}";
    }
};
int record(int argc, wchar_t** argv) {
    const bool withhold_file = argc > 1 && std::wstring_view{argv[1]} == L"--test-withhold-file-provider";
    const int offset = withhold_file ? 1 : 0;
    require(argc >= 4 + offset, "use pdb_file_event_probe OUTPUT-DIRECTORY EXECUTABLE [ARGV...]");
    for (const auto& expected : std::array<std::pair<const wchar_t*, const wchar_t*>, 3>{{
        {L"MQB_OWNERSHIP_DISPOSABLE_HOST", L"1"}, {L"GITHUB_ACTIONS", L"true"}, {L"RUNNER_ENVIRONMENT", L"github-hosted"}}}) {
        wchar_t value[128]{};
        require(::GetEnvironmentVariableW(expected.first, value, 128) > 0 && std::wstring_view{value} == expected.second,
                "event recording requires an explicitly disposable hosted VM");
    }
    const fs::path dir = fs::absolute(argv[1 + offset]);
    require(!fs::exists(dir), "refusing to overwrite an earlier trace"); fs::create_directories(dir);
    wchar_t device[32768]{};
    require(::QueryDosDeviceW(dir.root_name().c_str(), device, 32768) != 0, "QueryDosDevice failed");
    LARGE_INTEGER frequency{}; require(::QueryPerformanceFrequency(&frequency), "QPC frequency unavailable");
    ProfilePrivilege privilege;
    const auto file_mask = keyword_mask(file_provider, dir / "file-keywords.json", {L"KERNEL_FILE_KEYWORD_CREATE", L"KERNEL_FILE_KEYWORD_OP_END"});
    const auto process_mask = keyword_mask(process_provider, dir / "process-keywords.json", {L"WINEVENT_KEYWORD_PROCESS", L"WINEVENT_KEYWORD_THREAD"});
    const auto etl = dir / "events.etl";
    const auto session_name = L"MQB-PdbFileTrace-" + std::to_wstring(::GetCurrentProcessId()) + L"-" + std::to_wstring(qpc());
    Session session{etl, session_name};
    LiveReadiness live{session_name, device, dir.root_name().wstring(), frequency.QuadPart};
    std::string failure, before = "null", after = "null", child = "null";
    try {
        // Provider registration/configuration is not recording readiness. Observe
        // a complete real event pair, then acknowledge the single opening marker.
        live.start();
        if (!withhold_file) session.enable(file_provider, file_mask);
        session.enable(process_provider, process_mask);
        live.wait_provider();
        live.arm(dir / "calibration-before.pdb");
        CalibrationWindow opening{};
        before = calibration(dir / "calibration-before.pdb", &opening);
        live.wait_calibration(opening);
        const fs::path executable = fs::absolute(argv[2 + offset]);
        std::vector<std::wstring> arguments;
        for (int i = 3 + offset; i < argc; ++i) arguments.emplace_back(argv[i]);
        auto command = mqb::platform::windows::build_command_line(executable.wstring(), arguments);
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION information{};
        // This test launcher owns only the root HANDLE. The existing ownership
        // probe owns its fixture-wide Job. No new compiler/service kill policy.
        const auto child_start = qpc();
        const BOOL launched = ::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &information);
        const DWORD launch_error = launched ? ERROR_SUCCESS : ::GetLastError();
        checked(launch_error, "CreateProcessW trace child");
        Handle process{information.hProcess}, thread{information.hThread};
        FILETIME created{}, exited{}, kernel{}, user{};
        require(::GetProcessTimes(process.value, &created, &exited, &kernel, &user), "child identity failed");
        require(::WaitForSingleObject(process.value, INFINITE) == WAIT_OBJECT_0, "child wait failed; cleanup unproven");
        DWORD code{}; require(::GetExitCodeProcess(process.value, &code), "child exit query failed");
        child = "{\"pid\":" + std::to_string(information.dwProcessId) + ",\"created_filetime\":" + std::to_string(filetime(created))
            + ",\"before_launch_qpc\":" + std::to_string(child_start) + ",\"after_wait_qpc\":" + std::to_string(qpc())
            + ",\"exit_code\":" + std::to_string(code) + "}";
        after = calibration(dir / "calibration-after.pdb");
    } catch (const std::exception& e) { failure = e.what(); }
    session.stop();
    live.finish(); // CloseTrace targets the consumer, not the controller session.
    const auto readiness = live.report(withhold_file);
    save(dir / "readiness.json", readiness + "\n");
    auto* health = session.properties();
    // Stop result/loss counters survive even if TDH decoding subsequently fails.
    save(dir / "capture.json", "{\"schema\":2,\"session\":" + js(utf8(session_name))
        + ",\"dos_root\":" + js(utf8(dir.root_name().wstring())) + ",\"device_root\":" + js(utf8(device))
        + ",\"qpc_frequency\":" + std::to_string(frequency.QuadPart)
        + ",\"clock\":\"QPC\",\"file_mask\":" + std::to_string(file_mask) + ",\"process_mask\":" + std::to_string(process_mask)
        + ",\"stop_status\":" + std::to_string(session.stop_status) + ",\"events_lost\":" + std::to_string(health->EventsLost)
        + ",\"log_buffers_lost\":" + std::to_string(health->LogBuffersLost) + ",\"realtime_buffers_lost\":" + std::to_string(health->RealTimeBuffersLost)
        + ",\"readiness\":" + readiness + ",\"maximum_file_mb\":256,\"child\":" + child + ",\"before\":" + before + ",\"after\":" + after
        + ",\"failure\":" + (failure.empty() ? "null" : js(failure))
        + ",\"historical_cause_resolved\":false,\"safe_to_transfer_write_lease\":false}\n");
    checked(session.stop_status, "ControlTrace stop");
    Decoder decoder{dir / "events.jsonl"};
    auto etl_path = etl.wstring(); EVENT_TRACE_LOGFILEW log{}; log.LogFileName = etl_path.data();
    log.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_RAW_TIMESTAMP;
    log.EventRecordCallback = Decoder::callback; log.Context = &decoder;
    auto trace = ::OpenTraceW(&log); require(trace != INVALID_PROCESSTRACE_HANDLE, "OpenTrace failed");
    const auto result = ::ProcessTrace(&trace, 1, nullptr, nullptr);
    const auto closed = ::CloseTrace(trace); decoder.output.flush();
    save(dir / "decode.json", "{\"process_trace_status\":" + std::to_string(result) + ",\"close_trace_status\":" + std::to_string(closed)
        + ",\"total_events\":" + std::to_string(decoder.total) + ",\"selected_events\":" + std::to_string(decoder.selected)
        + ",\"decode_errors\":" + std::to_string(decoder.errors) + ",\"capped_events\":" + std::to_string(decoder.capped)
        + ",\"etl_bytes\":" + std::to_string(fs::file_size(etl)) + ",\"log_header_events_lost\":" + std::to_string(log.LogfileHeader.EventsLost) + "}\n");
    require(failure.empty() && live.healthy() && result == ERROR_SUCCESS && closed == ERROR_SUCCESS && decoder.errors == 0 && decoder.capped == 0 &&
        bool(decoder.output) && health->EventsLost == 0 && health->LogBuffersLost == 0 && health->RealTimeBuffersLost == 0 &&
        log.LogfileHeader.EventsLost == 0 && fs::file_size(etl) < 256ULL * 1024 * 1024, "capture incomplete; retain diagnostics, do not interpret missing events");
    return 0; // Collection only. Original child success is evaluated separately.
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 3 && std::wstring_view{argv[1]} == L"--test-child-sentinel") {
            save(fs::path{argv[2]}, "CHILD_WAS_ADMITTED\n");
            return 0;
        }
        return record(argc, argv);
    }
    catch (const std::exception& e) { std::cerr << "FILE_TRACE_INFRASTRUCTURE_FAILURE " << e.what() << '\n'; return 1; }
}
