// One bounded, non-product WRITE_RESTRICTED / named-pipe prerequisite.
// This fixture can falsify a candidate; it cannot issue an MQB receipt.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winternl.h>
#include <aclapi.h>
#include <sddl.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "advapi32.lib")

namespace {
bool emit_records = true;
unsigned errors = 0;
unsigned sequence = 0;
constexpr std::uint64_t baseline_time = 132537600000000000ULL;
constexpr DWORD all_sharing = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
constexpr DWORD protocol_magic = 0x4d514236;
constexpr DWORD protocol_leaf = 1, protocol_open = 2, protocol_close = 3, protocol_exit = 4;
constexpr DWORD phase_open = 1, phase_payload = 2, phase_close = 3, phase_exit = 4;

std::string escaped(const std::string& value) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string result = "\"";
    for (const unsigned char ch : value) {
        if (ch == '\\' || ch == '"') { result += '\\'; result += static_cast<char>(ch); }
        else if (ch < 32) {
            result += "\\u00"; result += hex[ch >> 4]; result += hex[ch & 15];
        } else { result += static_cast<char>(ch); }
    }
    return result + "\"";
}

std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    if (value.size() > 32768) throw std::runtime_error("UTF-16 string exceeds bound");
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) throw std::runtime_error("UTF-16 conversion failed");
    std::string result(static_cast<std::size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), count, nullptr, nullptr) != count)
        throw std::runtime_error("UTF-16 conversion incomplete");
    return result;
}

class Record {
    std::ostringstream data;
public:
    explicit Record(const char* kind) {
        data << "{\"kind\":" << escaped(kind);
        if (emit_records) data << ",\"sequence\":" << ++sequence;
    }
    Record(const Record&) = delete;
    ~Record() { if (emit_records) std::cout << data.str() << "}\n" << std::flush; }
    Record& text(const char* key, const std::string& value) {
        data << ',' << escaped(key) << ':' << escaped(value); return *this;
    }
    Record& wide(const char* key, const std::wstring& value) { return text(key, utf8(value)); }
    Record& number(const char* key, std::uint64_t value) {
        data << ',' << escaped(key) << ':' << value; return *this;
    }
    Record& boolean(const char* key, bool value) {
        data << ',' << escaped(key) << ':' << (value ? "true" : "false"); return *this;
    }
    Record& raw(const char* key, const std::string& value) {
        data << ',' << escaped(key) << ':' << value; return *this;
    }
};

void require(bool condition, const char* label) {
    Record("assertion").text("label", label).boolean("ok", condition);
    if (!condition) { ++errors; throw std::runtime_error(label); }
}

void checked(BOOL ok, const char* operation, const char* label) {
    const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    Record("api").text("operation", operation).text("label", label)
        .boolean("ok", ok != FALSE).number("error", error);
    if (!ok) { ++errors; throw std::runtime_error(label); }
}

bool valid(HANDLE value) { return value != nullptr && value != INVALID_HANDLE_VALUE; }
std::string handle_decimal(HANDLE value) {
    return std::to_string(reinterpret_cast<std::uintptr_t>(value));
}

class Handle {
    HANDLE value = nullptr;
    const char* label = "handle";
public:
    Handle() = default;
    Handle(HANDLE initial, const char* name) : value(initial), label(name) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(other.value), label(other.label) { other.value = nullptr; }
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) { close(); value = other.value; label = other.label; other.value = nullptr; }
        return *this;
    }
    ~Handle() { close(); }
    HANDLE get() const { return value; }
    bool close(DWORD* error_out = nullptr) noexcept {
        if (error_out) *error_out = ERROR_SUCCESS;
        if (!valid(value)) { value = nullptr; return true; }
        const HANDLE old = value; value = nullptr;
        const BOOL ok = CloseHandle(old);
        const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
        if (error_out) *error_out = error;
        Record("close").text("label", label).text("handle_decimal", handle_decimal(old))
            .boolean("ok", ok != FALSE).number("error", error);
        if (!ok) ++errors;
        return ok != FALSE;
    }
};

struct LocalMemory {
    HLOCAL value = nullptr;
    LocalMemory() = default;
    LocalMemory(const LocalMemory&) = delete;
    ~LocalMemory() {
        if (value && LocalFree(value) != nullptr) {
            ++errors; Record("local_free_error").number("error", GetLastError());
        }
    }
};

std::vector<std::byte> token_information(HANDLE token, TOKEN_INFORMATION_CLASS kind) {
    DWORD needed = 0;
    const BOOL first = GetTokenInformation(token, kind, nullptr, 0, &needed);
    const DWORD first_error = first ? 0 : GetLastError();
    Record("token_query_size").number("class", static_cast<DWORD>(kind))
        .boolean("ok", first != FALSE).number("error", first_error).number("bytes", needed);
    require(!first && first_error == ERROR_INSUFFICIENT_BUFFER && needed > 0 && needed <= 65536,
        "bounded token-information sizing");
    std::vector<std::byte> result(needed);
    DWORD returned = 0;
    checked(GetTokenInformation(token, kind, result.data(), needed, &returned),
        "GetTokenInformation", "token-information full read");
    require(returned > 0 && returned <= needed, "complete token-information size");
    result.resize(returned);
    return result;
}

bool within(const std::vector<std::byte>& buffer, const void* pointer, std::size_t bytes) {
    const auto first = reinterpret_cast<std::uintptr_t>(buffer.data());
    const auto actual = reinterpret_cast<std::uintptr_t>(pointer);
    return actual >= first && actual - first <= buffer.size() && bytes <= buffer.size() - (actual - first);
}

std::wstring sid_string(PSID sid) {
    require(IsValidSid(sid) != FALSE, "valid SID");
    LPWSTR text = nullptr;
    checked(ConvertSidToStringSidW(sid, &text), "ConvertSidToStringSidW", "SID rendering");
    LocalMemory memory; memory.value = text;
    return text;
}

std::wstring token_user(HANDLE token) {
    const auto buffer = token_information(token, TokenUser);
    require(buffer.size() >= sizeof(TOKEN_USER), "TOKEN_USER header");
    const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
    require(within(buffer, user->User.Sid, sizeof(SID)), "TOKEN_USER SID pointer");
    require(within(buffer, user->User.Sid, GetLengthSid(user->User.Sid)), "TOKEN_USER complete SID");
    return sid_string(user->User.Sid);
}

std::vector<std::wstring> token_sids(HANDLE token, TOKEN_INFORMATION_CLASS kind, const char* label) {
    const auto buffer = token_information(token, kind);
    require(buffer.size() >= sizeof(DWORD), "TOKEN_GROUPS header");
    const auto* groups = reinterpret_cast<const TOKEN_GROUPS*>(buffer.data());
    require(groups->GroupCount <= 128 && (groups->GroupCount == 0 || within(buffer, groups->Groups,
        static_cast<std::size_t>(groups->GroupCount) * sizeof(SID_AND_ATTRIBUTES))), "bounded TOKEN_GROUPS array");
    std::vector<std::wstring> result;
    for (DWORD i = 0; i < groups->GroupCount; ++i) {
        const auto& group = groups->Groups[i];
        require(within(buffer, group.Sid, sizeof(SID)), "group SID pointer");
        require(within(buffer, group.Sid, GetLengthSid(group.Sid)), "group complete SID");
        result.push_back(sid_string(group.Sid));
        Record("token_sid").text("label", label).number("index", i)
            .wide("sid", result.back()).number("attributes", group.Attributes);
    }
    Record("token_sids").text("label", label).number("count", result.size());
    return result;
}

// TokenSecurityAttributes uses counted UNICODE_STRING, not the public CLAIM
// structure's PWSTR. This is the layout consumed by Chromium AccessToken.
struct NativeAttribute {
    UNICODE_STRING name;
    USHORT type;
    USHORT reserved;
    ULONG flags;
    ULONG count;
    union { PULONG64 integers; PUNICODE_STRING strings; } values;
};
struct NativeAttributes { USHORT version; USHORT reserved; ULONG count; NativeAttribute* values; };
struct Claim {
    USHORT type = 0;
    ULONG flags = 0;
    std::vector<std::uint64_t> values;
    bool operator==(const Claim& other) const {
        return type == other.type && flags == other.flags && values == other.values;
    }
    std::wstring expression() const {
        std::wstring result = L"(TSA://ProcUnique == {";
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i) result += L",";
            result += std::to_wstring(values[i]);
        }
        return result + L"})";
    }
};

Claim actual_claim(HANDLE token, const char* label, bool must_exist = true) {
    const auto buffer = token_information(token, TokenSecurityAttributes);
    require(buffer.size() >= sizeof(NativeAttributes), "security attributes header");
    const auto* info = reinterpret_cast<const NativeAttributes*>(buffer.data());
    require(info->version == 1 && info->count <= 64 && (info->count == 0 || within(buffer, info->values,
        static_cast<std::size_t>(info->count) * sizeof(NativeAttribute))), "security attributes array");
    Claim result;
    unsigned matches = 0;
    for (ULONG i = 0; i < info->count; ++i) {
        const auto& attribute = info->values[i];
        require(attribute.name.Length % sizeof(wchar_t) == 0 &&
            within(buffer, attribute.name.Buffer, attribute.name.Length), "counted attribute name");
        const std::wstring name(attribute.name.Buffer, attribute.name.Length / sizeof(wchar_t));
        if (name != L"TSA://ProcUnique") continue;
        ++matches;
        require(attribute.type == 2 && attribute.count > 0 && attribute.count <= 8 &&
            within(buffer, attribute.values.integers, static_cast<std::size_t>(attribute.count) * sizeof(ULONG64)),
            "existing ProcUnique UInt64 value array");
        result.type = attribute.type; result.flags = attribute.flags;
        result.values.assign(attribute.values.integers, attribute.values.integers + attribute.count);
    }
    require(matches == 1 || (!must_exist && matches == 0), "actual ProcUnique attribute cardinality");
    std::string array = "[";
    for (std::size_t i = 0; i < result.values.size(); ++i) {
        if (i) array += ',';
        array += escaped(std::to_string(result.values[i]));
    }
    array += ']';
    Record("proc_unique").text("label", label).boolean("present", matches == 1).number("type", result.type)
        .number("flags", result.flags).raw("values_decimal", array)
        .wide("condition", result.expression());
    return result;
}

std::uint64_t luid_value(const LUID& value) {
    return (static_cast<std::uint64_t>(static_cast<DWORD>(value.HighPart)) << 32) | value.LowPart;
}

std::uint64_t authentication_id(HANDLE token, const char* label) {
    const auto buffer = token_information(token, TokenStatistics);
    require(buffer.size() >= sizeof(TOKEN_STATISTICS), "actual TOKEN_STATISTICS size");
    TOKEN_STATISTICS statistics{};
    std::memcpy(&statistics, buffer.data(), sizeof(statistics));
    Record("token_statistics").text("label", label)
        .text("authentication_id_decimal", std::to_string(luid_value(statistics.AuthenticationId)))
        .text("token_id_decimal", std::to_string(luid_value(statistics.TokenId)))
        .text("modified_id_decimal", std::to_string(luid_value(statistics.ModifiedId)))
        .number("token_type", static_cast<DWORD>(statistics.TokenType));
    return luid_value(statistics.AuthenticationId);
}

unsigned enabled_privileges(HANDLE token, const char* label, bool only_traverse = false) {
    const auto buffer = token_information(token, TokenPrivileges);
    require(buffer.size() >= offsetof(TOKEN_PRIVILEGES, Privileges), "privileges header");
    const auto* privileges = reinterpret_cast<const TOKEN_PRIVILEGES*>(buffer.data());
    require(privileges->PrivilegeCount <= 128 && within(buffer, privileges->Privileges,
        static_cast<std::size_t>(privileges->PrivilegeCount) * sizeof(LUID_AND_ATTRIBUTES)), "bounded privileges array");
    unsigned enabled = 0;
    LUID traverse{};
    if (only_traverse)
        checked(LookupPrivilegeValueW(nullptr, L"SeChangeNotifyPrivilege", &traverse),
            "LookupPrivilegeValueW", "only allowed child enabled privilege");
    for (DWORD i = 0; i < privileges->PrivilegeCount; ++i) {
        const auto& privilege = privileges->Privileges[i];
        if (privilege.Attributes & SE_PRIVILEGE_ENABLED) {
            ++enabled;
            if (only_traverse) require(luid_value(privilege.Luid) == luid_value(traverse),
                "child enabled privilege is only SeChangeNotifyPrivilege");
        }
        Record("token_privilege").text("label", label).number("index", i)
            .number("luid_low", privilege.Luid.LowPart)
            .number("luid_high_unsigned", static_cast<DWORD>(privilege.Luid.HighPart))
            .number("attributes", privilege.Attributes);
    }
    Record("token_privileges").text("label", label).number("enabled_count", enabled);
    return enabled;
}

struct Security {
    LocalMemory memory;
    SECURITY_ATTRIBUTES attributes{};
    explicit Security(const std::wstring& sddl) {
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        checked(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
            &descriptor, nullptr), "ConvertStringSecurityDescriptorToSecurityDescriptorW", "parse exact DACL");
        memory.value = descriptor;
        attributes.nLength = sizeof(attributes);
        attributes.lpSecurityDescriptor = descriptor;
        attributes.bInheritHandle = FALSE;
        require(IsValidSecurityDescriptor(descriptor) != FALSE, "valid explicit descriptor");
    }
    PACL dacl() const {
        BOOL present = FALSE, defaulted = FALSE; PACL result = nullptr;
        checked(GetSecurityDescriptorDacl(memory.value, &present, &result, &defaulted),
            "GetSecurityDescriptorDacl", "descriptor DACL");
        require(present && result != nullptr && !defaulted, "explicit non-NULL DACL");
        return result;
    }
};

std::wstring rendered_dacl(PSECURITY_DESCRIPTOR descriptor) {
    LPWSTR text = nullptr;
    checked(ConvertSecurityDescriptorToStringSecurityDescriptorW(descriptor, SDDL_REVISION_1,
        DACL_SECURITY_INFORMATION, &text, nullptr), "ConvertSecurityDescriptorToStringSecurityDescriptorW", "render DACL");
    LocalMemory memory; memory.value = text;
    return text;
}

void verify_dacl(HANDLE handle, const Security& expected, const char* label) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const DWORD error = GetSecurityInfo(handle, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
        nullptr, nullptr, nullptr, nullptr, &descriptor);
    LocalMemory memory; memory.value = descriptor;
    Record("get_security").text("label", label).number("error", error);
    require(error == ERROR_SUCCESS && descriptor != nullptr, "read actual DACL");
    SECURITY_DESCRIPTOR_CONTROL control = 0; DWORD revision = 0;
    checked(GetSecurityDescriptorControl(descriptor, &control, &revision),
        "GetSecurityDescriptorControl", "actual DACL control");
    const auto actual = rendered_dacl(descriptor);
    const auto wanted = rendered_dacl(expected.memory.value);
    Record("dacl").text("label", label).number("control", control)
        .wide("actual", actual).wide("expected", wanted);
    require((control & SE_DACL_PROTECTED) != 0 && actual == wanted, "actual protected DACL exact roundtrip");
}

struct FileIdentity {
    DWORD volume = 0;
    std::uint64_t id = 0;
    std::uint64_t time = 0;
    std::uint64_t size = 0;
};

FileIdentity file_identity(HANDLE handle, const char* label) {
    BY_HANDLE_FILE_INFORMATION info{};
    checked(GetFileInformationByHandle(handle, &info), "GetFileInformationByHandle", label);
    FileIdentity result{info.dwVolumeSerialNumber,
        (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow,
        (static_cast<std::uint64_t>(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime,
        (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow};
    Record("file_identity").text("label", label).number("volume", result.volume)
        .text("file_id_decimal", std::to_string(result.id)).text("last_write_decimal", std::to_string(result.time))
        .text("size_decimal", std::to_string(result.size)).number("links", info.nNumberOfLinks)
        .number("attributes", info.dwFileAttributes);
    require(info.nNumberOfLinks == 1 && result.size == 0 &&
        !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)),
        "regular single-link zero-data fixture leaf");
    return result;
}

Handle open_leaf(const std::wstring& path, DWORD access, const char* label, DWORD& error) {
    const HANDLE handle = CreateFileW(path.c_str(), access, all_sharing, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    error = valid(handle) ? ERROR_SUCCESS : GetLastError();
    Record("leaf_open").text("label", label).wide("path", path).number("desired_access", access)
        .number("share", all_sharing).text("handle_decimal", handle_decimal(handle)).number("error", error);
    return Handle(handle, label);
}

FileIdentity read_leaf(const std::wstring& path, const char* label) {
    DWORD error = 0;
    auto handle = open_leaf(path, FILE_READ_ATTRIBUTES | READ_CONTROL, label, error);
    require(valid(handle.get()), "independent read-only leaf open");
    return file_identity(handle.get(), label);
}

void no_thread_token(const char* label) {
    HANDLE token = nullptr;
    const BOOL ok = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token);
    const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    Handle owned(token, "unexpected-thread-token");
    Record("no_thread_token").text("label", label).boolean("opened", ok != FALSE).number("error", error);
    require(!ok && error == ERROR_NO_TOKEN, "server uses its own primary token");
}

struct Command { DWORD magic; DWORD command; DWORD index; DWORD reserved; };
struct Report {
    DWORD magic; DWORD command; DWORD index; DWORD phase;
    DWORD attempted; DWORD success; DWORD error; DWORD transferred;
    std::uint64_t handle; std::uint64_t time;
};
static_assert(sizeof(Command) == 16);
static_assert(sizeof(Report) == 48);

void send_command(HANDLE handle, DWORD command, DWORD index) {
    const Command value{protocol_magic, command, index, 0}; DWORD written = 0;
    checked(WriteFile(handle, &value, sizeof(value), &written, nullptr), "WriteFile", "bounded command");
    require(written == sizeof(value), "complete command write");
}

Report receive_report(HANDLE handle, DWORD command, DWORD index, DWORD phase) {
    Report report{}; DWORD read = 0;
    checked(ReadFile(handle, &report, sizeof(report), &read, nullptr), "ReadFile", "bounded child report");
    Record("child_report").number("command", report.command).number("case", report.index)
        .number("phase", report.phase).boolean("attempted", report.attempted != 0)
        .boolean("success", report.success != 0).number("error", report.error).number("transferred", report.transferred)
        .text("handle_decimal", std::to_string(report.handle)).text("time_decimal", std::to_string(report.time));
    require(read == sizeof(report) && report.magic == protocol_magic && report.command == command &&
        report.index == index && report.phase == phase && report.attempted <= 1 && report.success <= 1,
        "exact fixed child protocol");
    return report;
}

bool child_report(HANDLE pipe, DWORD command, DWORD index, DWORD phase, bool attempted,
    BOOL success, DWORD error, DWORD transferred, HANDLE handle = nullptr, std::uint64_t time = 0) {
    const Report report{protocol_magic, command, index, phase, attempted ? 1UL : 0UL,
        success ? 1UL : 0UL, error, transferred, reinterpret_cast<std::uintptr_t>(handle), time};
    DWORD written = 0;
    return WriteFile(pipe, &report, sizeof(report), &written, nullptr) && written == sizeof(report);
}

constexpr std::array<DWORD, 4> pipe_access{GENERIC_WRITE, GENERIC_READ, GENERIC_READ, GENERIC_WRITE};
constexpr std::array<DWORD, 4> pipe_mode{PIPE_ACCESS_DUPLEX, PIPE_ACCESS_OUTBOUND, PIPE_ACCESS_OUTBOUND, PIPE_ACCESS_DUPLEX};
constexpr std::array<DWORD, 4> pipe_sqos{
    SECURITY_SQOS_PRESENT | SECURITY_IMPERSONATION,
    SECURITY_SQOS_PRESENT | SECURITY_IMPERSONATION,
    SECURITY_SQOS_PRESENT | SECURITY_IMPERSONATION | SECURITY_CONTEXT_TRACKING,
    SECURITY_SQOS_PRESENT | SECURITY_IMPERSONATION};

std::wstring pipe_name(const std::wstring& stem, DWORD index, bool baseline = false) {
    return stem + L"-" + std::to_wstring(index) + (baseline ? L"-baseline" : L"-actual");
}

HANDLE parsed_handle(const wchar_t* text) {
    if (*text < L'1' || *text > L'9') return nullptr;
    wchar_t* end = nullptr;
    const auto value = std::wcstoull(text, &end, 10);
    if (!end || *end != L'\0' || value == 0 || value == UINT64_MAX) return nullptr;
    return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(value));
}

int run_client(const std::wstring& leaf, const std::wstring& stem, HANDLE input, HANDLE output) {
    if (!valid(input) || !valid(output) || input == output) return 3;
    Handle input_owner(input, "client-command"), output_owner(output, "client-report");
    DWORD flags = 0;
    if (!GetHandleInformation(input, &flags) || !(flags & HANDLE_FLAG_INHERIT) ||
        !GetHandleInformation(output, &flags) || !(flags & HANDLE_FLAG_INHERIT)) return 3;
    Handle connection;
    DWORD current = 0;
    for (unsigned step = 0; step < 10; ++step) {
        Command command{}; DWORD read = 0;
        if (!ReadFile(input, &command, sizeof(command), &read, nullptr) || read != sizeof(command) ||
            command.magic != protocol_magic || command.reserved != 0) return 3;
        if (command.command == protocol_leaf && step == 0 && command.index == 0) {
            const HANDLE handle = CreateFileW(leaf.c_str(), FILE_WRITE_ATTRIBUTES, all_sharing, nullptr,
                OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            const DWORD error = valid(handle) ? 0 : GetLastError();
            if (!child_report(output, protocol_leaf, 0, phase_open, true, valid(handle), error, 0, handle)) return 3;
            const FILETIME time{static_cast<DWORD>(baseline_time), static_cast<DWORD>(baseline_time >> 32)};
            const BOOL changed = valid(handle) ? SetFileTime(handle, nullptr, nullptr, &time) : FALSE;
            const DWORD changed_error = valid(handle) ? (changed ? 0 : GetLastError()) : error;
            if (!child_report(output, protocol_leaf, 0, phase_payload, valid(handle), changed,
                changed_error, 0, handle, baseline_time)) { if (valid(handle)) CloseHandle(handle); return 3; }
            const BOOL closed = valid(handle) ? CloseHandle(handle) : FALSE;
            const DWORD close_error = valid(handle) ? (closed ? 0 : GetLastError()) : error;
            if (!child_report(output, protocol_leaf, 0, phase_close, valid(handle), closed, close_error, 0, handle)) return 3;
            if (!changed || !closed) return 3;
        } else if (command.command == protocol_open && command.index == current && current < 4 &&
                   step == 1 + current * 2) {
            const auto name = pipe_name(stem, current);
            const HANDLE handle = CreateFileW(name.c_str(), pipe_access[current], 0, nullptr,
                OPEN_EXISTING, pipe_sqos[current], nullptr);
            const DWORD error = valid(handle) ? 0 : GetLastError();
            connection = Handle(handle, "client-pipe");
            if (!child_report(output, protocol_open, current, phase_open, true, valid(handle), error, 0, handle)) return 3;
            if (valid(handle)) {
                unsigned char byte = 0x40; DWORD transferred = 0;
                const BOOL ok = pipe_access[current] == GENERIC_READ
                    ? ReadFile(handle, &byte, 1, &transferred, nullptr)
                    : WriteFile(handle, &byte, 1, &transferred, nullptr);
                const DWORD io_error = ok ? 0 : GetLastError();
                if (!child_report(output, protocol_open, current, phase_payload, true, ok, io_error,
                    transferred, handle, byte)) return 3;
                if (!ok || transferred != 1 || byte != 0x40) return 3;
            }
        } else if (command.command == protocol_close && command.index == current && current < 4 &&
                   step == 2 + current * 2) {
            const bool attempted = valid(connection.get());
            const HANDLE old = connection.get();
            DWORD close_error = 0;
            const bool closed = connection.close(&close_error);
            if (!child_report(output, protocol_close, current, phase_close, attempted, closed,
                close_error, 0, old)) return 3;
            ++current;
        } else if (command.command == protocol_exit && command.index == 0 && step == 9 && current == 4) {
            if (!child_report(output, protocol_exit, 0, phase_exit, true, TRUE, 0, 0)) return 3;
            const bool input_closed = input_owner.close();
            const bool output_closed = output_owner.close();
            return input_closed && output_closed && errors == 0 ? 0 : 3;
        } else return 3;
    }
    return 3;
}

std::wstring quote_argument(const std::wstring& value) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'"') result.append(slashes * 2 + 1, L'\\');
        else result.append(slashes, L'\\');
        result += ch; slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L"\"";
}

struct AttributeList {
    std::vector<std::byte> bytes;
    LPPROC_THREAD_ATTRIBUTE_LIST value = nullptr;
    AttributeList() {
        SIZE_T needed = 0;
        const BOOL ok = InitializeProcThreadAttributeList(nullptr, 2, 0, &needed);
        const DWORD error = ok ? 0 : GetLastError();
        Record("attribute_list_size").boolean("ok", ok != FALSE).number("error", error).number("bytes", needed);
        require(!ok && error == ERROR_INSUFFICIENT_BUFFER && needed > 0 && needed <= 65536,
            "bounded startup attribute list");
        bytes.resize(needed);
        value = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(bytes.data());
        if (!InitializeProcThreadAttributeList(value, 2, 0, &needed)) {
            value = nullptr; checked(FALSE, "InitializeProcThreadAttributeList", "startup attribute list");
        }
    }
    ~AttributeList() { if (value) DeleteProcThreadAttributeList(value); }
};

struct CaseResult {
    bool client_opened = false;
    DWORD open_error = 0;
    bool impersonated = false;
    DWORD impersonation_error = 0;
    bool claim_values_match = false;
    bool claim_all_fields_match = false;
    bool write_opened = false;
    DWORD write_error = 0;
    bool mutation_verified = false;
    Handle writer;
};

struct Summary {
    bool positive_control = false;
    bool client_natural_exit = false;
    DWORD client_job_total = 0;
    DWORD client_job_active = 0;
    bool acl_revoked = false;
    unsigned cases_completed = 0;
    bool disqualified = false;
    bool write_connection_refused = false;
    std::array<CaseResult, 4> cases;
};

Handle named_server(const std::wstring& name, DWORD index, Security& security, const char* label) {
    const HANDLE handle = CreateNamedPipeW(name.c_str(), pipe_mode[index] | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, 64, 64, 0, &security.attributes);
    const DWORD error = valid(handle) ? 0 : GetLastError();
    Record("named_pipe_create").text("label", label).number("case", index).wide("name", name)
        .number("open_mode", pipe_mode[index] | FILE_FLAG_FIRST_PIPE_INSTANCE)
        .number("pipe_mode", PIPE_REJECT_REMOTE_CLIENTS).number("max_instances", 1)
        .text("handle_decimal", handle_decimal(handle)).number("error", error);
    require(valid(handle), "fresh named pipe creation");
    return Handle(handle, label);
}

void connect_already_open_client(HANDLE server, const char* label) {
    // The client reports its successful open while retaining that connection.
    // ConnectNamedPipe documents ERROR_PIPE_CONNECTED for this exact ordering.
    const BOOL ok = ConnectNamedPipe(server, nullptr);
    const DWORD error = ok ? 0 : GetLastError();
    Record("pipe_connect").text("label", label).boolean("ok", ok != FALSE).number("error", error);
    require(ok || error == ERROR_PIPE_CONNECTED, "acknowledge already-connected client");
}

void baseline_pipe(const std::wstring& stem, DWORD index, Security& security) {
    const auto name = pipe_name(stem, index, true);
    auto server = named_server(name, index, security, "independent-baseline-server");
    verify_dacl(server.get(), security, "baseline-pipe-DACL");
    const HANDLE client = CreateFileW(name.c_str(), pipe_access[index], 0, nullptr,
        OPEN_EXISTING, pipe_sqos[index], nullptr);
    const DWORD error = valid(client) ? 0 : GetLastError();
    Handle owned(client, "independent-baseline-client");
    Record("baseline_pipe_open").number("case", index).number("desired_access", pipe_access[index])
        .number("sqos_flags", pipe_sqos[index]).boolean("opened", valid(client)).number("error", error);
    require(valid(client), "same-user unrestricted baseline open with exact direction and access");
    connect_already_open_client(server.get(), "independent-baseline-connect");
    require(owned.close(), "baseline client close");
    checked(DisconnectNamedPipe(server.get()), "DisconnectNamedPipe", "independent baseline disconnect");
    require(server.close(), "independent baseline instance destroyed");
}

void inspect_impersonation(HANDLE server, const std::wstring& leaf, const Claim& client_claim,
    const std::wstring& user, const std::wstring& restricting_sid, std::uint64_t expected_authentication,
    DWORD index, CaseResult& result) {
    no_thread_token("before exact pipe impersonation");
    const BOOL impersonated = ImpersonateNamedPipeClient(server);
    const DWORD error = impersonated ? 0 : GetLastError();
    result.impersonated = impersonated != FALSE;
    result.impersonation_error = error;
    Record("impersonate_pipe").number("case", index).boolean("ok", impersonated != FALSE).number("error", error);
    if (!impersonated) {
        no_thread_token("failed impersonation retained no token");
        require(error == ERROR_CANNOT_IMPERSONATE || error == ERROR_ACCESS_DENIED ||
            error == ERROR_PRIVILEGE_NOT_HELD, "recognized exact-route impersonation refusal");
        return;
    }
    // Always revert before unwinding, including token-query or allocation errors.
    struct Revert {
        bool active = true;
        ~Revert() {
            if (active) {
                const BOOL ok = RevertToSelf(); const DWORD code = ok ? 0 : GetLastError();
                Record("revert_on_unwind").boolean("ok", ok != FALSE).number("error", code);
                if (!ok) { ++errors; TerminateProcess(GetCurrentProcess(), 3); }
            }
        }
    } guard;
    HANDLE raw = nullptr;
    checked(OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &raw),
        "OpenThreadToken", "actual IPC thread token query-only handle");
    Handle token(raw, "actual-IPC-thread-token");
    const auto type_buffer = token_information(token.get(), TokenType);
    require(type_buffer.size() >= sizeof(TOKEN_TYPE), "actual token type size");
    TOKEN_TYPE type{}; std::memcpy(&type, type_buffer.data(), sizeof(type));
    require(type == TokenImpersonation, "actual thread token is impersonation type");
    const auto level_buffer = token_information(token.get(), TokenImpersonationLevel);
    require(level_buffer.size() >= sizeof(SECURITY_IMPERSONATION_LEVEL), "actual impersonation level size");
    SECURITY_IMPERSONATION_LEVEL level{}; std::memcpy(&level, level_buffer.data(), sizeof(level));
    const auto actual = actual_claim(token.get(), "actual-pipe-thread-token", false);
    const auto actual_user = token_user(token.get());
    const auto restrictions = token_sids(token.get(), TokenRestrictedSids, "actual-pipe-token-restrictions");
    result.claim_values_match = actual.type == client_claim.type && actual.values == client_claim.values;
    result.claim_all_fields_match = actual == client_claim;
    Record("impersonated_identity").number("case", index).wide("user", actual_user)
        .number("token_type", static_cast<DWORD>(type)).number("impersonation_level", static_cast<DWORD>(level))
        .boolean("claim_values_match", result.claim_values_match)
        .boolean("claim_all_fields_match", result.claim_all_fields_match)
        .boolean("exact_restricting_sid", restrictions.size() == 1 && restrictions[0] == restricting_sid);
    require(actual_user == user, "IPC uses the same authenticated user");
    require(authentication_id(token.get(), "actual-IPC-token") == expected_authentication,
        "actual IPC token preserves the same authentication context");
    require(restrictions.size() == 1 && restrictions[0] == restricting_sid, "IPC retains actual write-restricted SID");
    DWORD open_error = 0;
    auto writer = open_leaf(leaf, FILE_WRITE_ATTRIBUTES, "server-under-client-IPC-token", open_error);
    result.write_opened = valid(writer.get()); result.write_error = open_error;
    if (result.write_opened) {
        require(level >= SecurityImpersonation && result.claim_values_match,
            "granted conditional write binds actual admitted client values");
        result.writer = std::move(writer);
    } else {
        require(open_error == ERROR_ACCESS_DENIED || open_error == ERROR_BAD_IMPERSONATION_LEVEL,
            "recognized conditional leaf refusal");
    }
    require(token.close(), "query-only IPC token handle closed");
    const BOOL reverted = RevertToSelf();
    const DWORD revert_error = reverted ? 0 : GetLastError();
    guard.active = false;
    Record("revert").boolean("ok", reverted != FALSE).number("error", revert_error);
    if (!reverted) { ++errors; TerminateProcess(GetCurrentProcess(), 3); throw std::runtime_error("failed to revert IPC token"); }
    no_thread_token("after exact pipe impersonation");
}

void run_server(const std::wstring& root, Summary& summary) {
    require(root.size() < 30000 && root.find(L'"') == std::wstring::npos, "bounded fixture root argument");
    checked(CreateDirectoryW(root.c_str(), nullptr), "CreateDirectoryW", "fresh fixture root");
    const std::wstring leaf = root + L"\\delegated-metadata.bin";
    HANDLE raw_token = nullptr;
    checked(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY |
        TOKEN_ADJUST_PRIVILEGES, &raw_token), "OpenProcessToken", "server actual primary token");
    Handle host_token(raw_token, "server-primary-token");
    const auto host_claim = actual_claim(host_token.get(), "server-primary-before-filter");
    const auto user = token_user(host_token.get());
    const auto host_authentication = authentication_id(host_token.get(), "server-primary");
    const auto groups = token_sids(host_token.get(), TokenGroups, "server-groups");
    require(token_sids(host_token.get(), TokenRestrictedSids, "server-restrictions").empty(),
        "unrestricted server baseline token");
    LUID unique{};
    checked(AllocateLocallyUniqueId(&unique), "AllocateLocallyUniqueId", "fresh restricting SID discriminator");
    const auto restricting_sid = L"S-1-5-21-" + std::to_wstring(static_cast<DWORD>(unique.HighPart)) +
        L"-" + std::to_wstring(unique.LowPart) + L"-" + std::to_wstring(GetCurrentProcessId()) + L"-1048577";
    require(user != restricting_sid, "restricting SID is not actual user");
    for (const auto& group : groups) require(group != restricting_sid, "restricting SID not an enabled group");
    PSID raw_sid = nullptr;
    checked(ConvertStringSidToSidW(restricting_sid.c_str(), &raw_sid), "ConvertStringSidToSidW", "fresh restricting SID");
    LocalMemory sid_owner; sid_owner.value = raw_sid;
    SID_AND_ATTRIBUTES restriction{raw_sid, 0};
    HANDLE raw_restricted = nullptr;
    constexpr DWORD restriction_flags = DISABLE_MAX_PRIVILEGE | WRITE_RESTRICTED;
    checked(CreateRestrictedToken(host_token.get(), restriction_flags, 0, nullptr, 0, nullptr,
        1, &restriction, &raw_restricted), "CreateRestrictedToken", "WRITE_RESTRICTED plus disabled privileges");
    Handle restricted(raw_restricted, "pre-birth-restricted-primary-token");
    Record("restricted_token_created").number("flags", restriction_flags).wide("restricting_sid", restricting_sid);
    require(actual_claim(restricted.get(), "pre-birth-filtered-token") == host_claim,
        "filtered pre-birth claim remains distinguishable from child claim");
    const auto before_restrictions = token_sids(restricted.get(), TokenRestrictedSids, "pre-birth-restrictions");
    require(before_restrictions.size() == 1 && before_restrictions[0] == restricting_sid,
        "exact single restricting SID before process birth");

    Handle job(CreateJobObjectW(nullptr, nullptr), "client-only-job");
    require(valid(job.get()), "create owned client-only Job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    limits.BasicLimitInformation.ActiveProcessLimit = 1;
    checked(SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)),
        "SetInformationJobObject", "one child, no child descendants, kill on owned close");
    BOOL server_in_job = TRUE;
    checked(IsProcessInJob(GetCurrentProcess(), job.get(), &server_in_job), "IsProcessInJob", "server outside client Job");
    require(!server_in_job, "server is not a client Job member");

    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE command_read_raw = nullptr, command_write_raw = nullptr;
    checked(CreatePipe(&command_read_raw, &command_write_raw, &inheritable, 4096), "CreatePipe", "owned command channel");
    Handle command_read(command_read_raw, "parent-command-read"), command_write(command_write_raw, "parent-command-write");
    checked(SetHandleInformation(command_write.get(), HANDLE_FLAG_INHERIT, 0), "SetHandleInformation", "parent command end not inherited");
    HANDLE report_read_raw = nullptr, report_write_raw = nullptr;
    checked(CreatePipe(&report_read_raw, &report_write_raw, &inheritable, 4096), "CreatePipe", "owned report channel");
    Handle report_read(report_read_raw, "parent-report-read"), report_write(report_write_raw, "parent-report-write");
    checked(SetHandleInformation(report_read.get(), HANDLE_FLAG_INHERIT, 0), "SetHandleInformation", "parent report end not inherited");
    const std::array<HANDLE, 2> inherited{command_read.get(), report_write.get()};
    const std::array<HANDLE, 1> birth_jobs{job.get()};
    AttributeList attributes;
    checked(UpdateProcThreadAttribute(attributes.value, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        const_cast<HANDLE*>(inherited.data()), sizeof(inherited), nullptr, nullptr),
        "UpdateProcThreadAttribute", "exact two protocol handles");
    checked(UpdateProcThreadAttribute(attributes.value, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST,
        const_cast<HANDLE*>(birth_jobs.data()), sizeof(birth_jobs), nullptr, nullptr),
        "UpdateProcThreadAttribute", "atomic birth into exact client-only Job");
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = command_read.get();
    startup.StartupInfo.hStdOutput = report_write.get();
    startup.StartupInfo.hStdError = report_write.get();
    startup.lpAttributeList = attributes.value;
    std::vector<wchar_t> image_buffer(32768);
    const DWORD image_length = GetModuleFileNameW(nullptr, image_buffer.data(), static_cast<DWORD>(image_buffer.size()));
    require(image_length > 0 && image_length < image_buffer.size(), "exact current fixture image path");
    const std::wstring image(image_buffer.data(), image_length);
    const auto stem = L"\\\\.\\pipe\\mqb-delegation-006-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
        std::to_wstring(static_cast<DWORD>(unique.HighPart)) + L"-" + std::to_wstring(unique.LowPart);
    auto command_line = quote_argument(image) + L" --client " + quote_argument(leaf) + L" " + quote_argument(stem) + L" " +
        std::to_wstring(reinterpret_cast<std::uintptr_t>(command_read.get())) + L" " +
        std::to_wstring(reinterpret_cast<std::uintptr_t>(report_write.get()));
    Security process_security(L"D:P(A;;RC;;;OW)(XA;;0x001fffff;;;" + user + L";" + host_claim.expression() + L")");
    PROCESS_INFORMATION process_info{};
    const DWORD creation_flags = CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT;
    checked(CreateProcessAsUserW(restricted.get(), image.c_str(), command_line.data(), &process_security.attributes,
        &process_security.attributes, TRUE, creation_flags, nullptr, root.c_str(),
        &startup.StartupInfo, &process_info), "CreateProcessAsUserW", "one suspended restricted client");
    Handle process(process_info.hProcess, "actual-client-process"), thread(process_info.hThread, "actual-client-thread");
    Record("client_created").number("pid", process_info.dwProcessId).number("tid", process_info.dwThreadId)
        .number("flags", creation_flags).number("inherited_handle_count", inherited.size()).wide("image", image)
        .wide("command_line", command_line).boolean("resumed", false);
    BOOL child_in_job = FALSE;
    checked(IsProcessInJob(process.get(), job.get(), &child_in_job), "IsProcessInJob", "actual client Job membership");
    require(child_in_job != FALSE, "client is in its owned Job before resume");
    HANDLE child_token_raw = nullptr;
    checked(OpenProcessToken(process.get(), TOKEN_QUERY, &child_token_raw), "OpenProcessToken", "suspended actual client primary token");
    Handle child_token(child_token_raw, "actual-client-primary-query-only");
    const auto client_claim = actual_claim(child_token.get(), "actual-child-primary-after-birth");
    require(client_claim.values != host_claim.values, "new process actual claim differs from server/filter token");
    require(token_user(child_token.get()) == user, "client and server share authenticated user");
    require(authentication_id(child_token.get(), "actual-child-primary") == host_authentication,
        "actual child has the server authentication context");
    const auto child_restrictions = token_sids(child_token.get(), TokenRestrictedSids, "actual-child-restrictions");
    require(child_restrictions.size() == 1 && child_restrictions[0] == restricting_sid, "actual child has exact restricting SID");
    const auto child_type_buffer = token_information(child_token.get(), TokenType);
    require(child_type_buffer.size() >= sizeof(TOKEN_TYPE), "child token type length");
    TOKEN_TYPE child_type{}; std::memcpy(&child_type, child_type_buffer.data(), sizeof(child_type));
    require(child_type == TokenPrimary, "actual child token is primary");
    require(enabled_privileges(child_token.get(), "actual-child-privileges", true) <= 1,
        "child has no extra enabled privilege");
    require(command_read.close() && report_write.close(), "parent closes child protocol ends");
    require(restricted.close() && child_token.close(), "no pre-birth or actual-client token handle retained for impersonation");

    const auto common_dacl = L"D:P(A;;RC;;;OW)(A;;0x00120089;;;" + user +
        L")(XA;;0x00040000;;;" + user + L";" + host_claim.expression() + L")";
    Security active_leaf(common_dacl + L"(XA;;0x00000100;;;" + user + L";" + client_claim.expression() +
        L")(A;;0x00000100;;;" + restricting_sid + L")");
    Security revoked_leaf(common_dacl);
    const HANDLE created_raw = CreateFileW(leaf.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL | WRITE_DAC,
        all_sharing, &active_leaf.attributes, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    const DWORD creation_error = valid(created_raw) ? 0 : GetLastError();
    Handle created(created_raw, "leaf-birth-handle");
    Record("leaf_create").wide("path", leaf).number("access", FILE_READ_ATTRIBUTES | READ_CONTROL | WRITE_DAC)
        .text("handle_decimal", handle_decimal(created_raw)).number("error", creation_error);
    require(valid(created_raw), "fresh zero-data conditional leaf");
    verify_dacl(created.get(), active_leaf, "leaf-birth-DACL");
    const auto identity = file_identity(created.get(), "leaf-at-birth");
    require(created.close(), "birth handle closed before fresh-open controls");
    DWORD denied_error = 0;
    auto denied = open_leaf(leaf, FILE_WRITE_ATTRIBUTES, "server-primary-WA-negative-before-client", denied_error);
    require(!valid(denied.get()) && denied_error == ERROR_ACCESS_DENIED, "server primary fresh WA refused");
    SetLastError(ERROR_SUCCESS);
    const BOOL disabled = AdjustTokenPrivileges(host_token.get(), TRUE, nullptr, 0, nullptr, nullptr);
    const DWORD disabled_error = GetLastError();
    Record("disable_server_privileges").boolean("ok", disabled != FALSE).number("error", disabled_error);
    require(disabled && disabled_error == ERROR_SUCCESS, "disable all privileges in this fixture server only");
    require(enabled_privileges(host_token.get(), "server-before-IPC") == 0, "no server privilege enabled for IPC");
    require(actual_claim(host_token.get(), "server-after-disabling-privileges") == host_claim, "server claim unchanged by privilege disable");
    no_thread_token("server before client resume");
    const DWORD suspend_count = ResumeThread(thread.get());
    const DWORD resume_error = suspend_count == static_cast<DWORD>(-1) ? GetLastError() : 0;
    Record("client_resume").number("previous_suspend_count", suspend_count).number("error", resume_error);
    require(suspend_count == 1, "single admitted client resume");
    require(thread.close(), "parent closes client thread handle after resume");

    send_command(command_write.get(), protocol_leaf, 0);
    const auto leaf_open = receive_report(report_read.get(), protocol_leaf, 0, phase_open);
    const auto leaf_write = receive_report(report_read.get(), protocol_leaf, 0, phase_payload);
    const auto leaf_close = receive_report(report_read.get(), protocol_leaf, 0, phase_close);
    require(leaf_open.attempted && leaf_open.success && leaf_open.error == 0 &&
        leaf_write.attempted && leaf_write.success && leaf_write.error == 0 && leaf_write.time == baseline_time &&
        leaf_close.attempted && leaf_close.success && leaf_close.error == 0,
        "actual restricted client conditional WA positive and checked close");
    const auto after_client_control = read_leaf(leaf, "independent-client-positive-readback");
    require(after_client_control.volume == identity.volume && after_client_control.id == identity.id &&
        after_client_control.time == baseline_time, "same leaf and exact client timestamp write");

    for (DWORD index = 0; index < 4; ++index) {
        auto& result = summary.cases[index];
        no_thread_token("fixed case begins without thread token");
        std::wstring pipe_dacl = L"D:P(A;;RC;;;OW)(A;;0x0012019f;;;" + user + L")";
        if (index == 3) pipe_dacl += L"(A;;0x0012019f;;;" + restricting_sid + L")";
        Security security(pipe_dacl);
        baseline_pipe(stem, index, security);
        auto server = named_server(pipe_name(stem, index), index, security, "actual-case-server");
        verify_dacl(server.get(), security, "actual-case-pipe-DACL");
        Record("case_begin").number("case", index).number("client_desired_access", pipe_access[index])
            .number("sqos_flags", pipe_sqos[index]).boolean("pipe_grants_restricting_sid", index == 3)
            .number("server_open_mode", pipe_mode[index]);
        send_command(command_write.get(), protocol_open, index);
        const auto opened = receive_report(report_read.get(), protocol_open, index, phase_open);
        require(opened.attempted != 0, "client attempted exact pipe open");
        result.client_opened = opened.success != 0; result.open_error = opened.error;
        if (result.client_opened) {
            require(opened.error == 0, "successful client pipe open has no error");
            connect_already_open_client(server.get(), "actual-client-connect");
            unsigned char byte = 0x40; DWORD transferred = 0;
            const BOOL io = pipe_access[index] == GENERIC_READ
                ? WriteFile(server.get(), &byte, 1, &transferred, nullptr)
                : ReadFile(server.get(), &byte, 1, &transferred, nullptr);
            checked(io, pipe_access[index] == GENERIC_READ ? "WriteFile" : "ReadFile", "one exact pipe payload byte");
            require(transferred == 1 && byte == 0x40, "complete exact one-byte pipe payload");
            const auto payload = receive_report(report_read.get(), protocol_open, index, phase_payload);
            require(payload.attempted && payload.success && payload.error == 0 && payload.transferred == 1 && payload.time == 0x40,
                "client completed corresponding one-byte payload");
            inspect_impersonation(server.get(), leaf, client_claim, user, restricting_sid, host_authentication, index, result);
        } else {
            require(opened.error == ERROR_ACCESS_DENIED, "exact pipe access refused with access denied");
        }
        send_command(command_write.get(), protocol_close, index);
        const auto closed = receive_report(report_read.get(), protocol_close, index, phase_close);
        require(closed.success && closed.error == 0 && (closed.attempted != 0) == result.client_opened,
            "client closes only its actual connection");
        if (result.client_opened)
            checked(DisconnectNamedPipe(server.get()), "DisconnectNamedPipe", "actual case disconnect after client close");
        require(server.close(), "exact case server handle closes");
        no_thread_token("fixed case ends without thread token");
        ++summary.cases_completed;
        Record("case_before_client_exit").number("case", index).boolean("client_opened", result.client_opened)
            .number("open_error", result.open_error).boolean("impersonated", result.impersonated)
            .number("impersonation_error", result.impersonation_error).boolean("claim_values_match", result.claim_values_match)
            .boolean("claim_all_fields_match", result.claim_all_fields_match).boolean("write_opened", result.write_opened)
            .number("write_error", result.write_error).boolean("WA_handle_still_held", valid(result.writer.get()));
    }
    send_command(command_write.get(), protocol_exit, 0);
    const auto exiting = receive_report(report_read.get(), protocol_exit, 0, phase_exit);
    require(exiting.success && exiting.error == 0, "client normal-exit acknowledgement");
    const DWORD waited = WaitForSingleObject(process.get(), 5000);
    Record("client_exit_wait").number("result", waited).number("error", waited == WAIT_FAILED ? GetLastError() : 0);
    require(waited == WAIT_OBJECT_0, "actual client process naturally signaled");
    DWORD exit_code = STILL_ACTIVE;
    checked(GetExitCodeProcess(process.get(), &exit_code), "GetExitCodeProcess", "actual client final exit code");
    require(exit_code == 0, "client completed all owned handle closes");
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
    checked(QueryInformationJobObject(job.get(), JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr),
        "QueryInformationJobObject", "actual client-only Job completion");
    summary.client_job_total = accounting.TotalProcesses; summary.client_job_active = accounting.ActiveProcesses;
    Record("client_job_completion").number("total_processes", accounting.TotalProcesses)
        .number("active_processes", accounting.ActiveProcesses).number("total_terminated_processes", accounting.TotalTerminatedProcesses)
        .number("client_exit_code", exit_code).boolean("termination_requested", false);
    require(accounting.TotalProcesses == 1 && accounting.ActiveProcesses == 0 && accounting.TotalTerminatedProcesses == 0,
        "one naturally completed client and no unaccounted Job member");
    summary.client_natural_exit = true;
    no_thread_token("before admission revocation");
    DWORD management_error = 0;
    auto management = open_leaf(leaf, WRITE_DAC | READ_CONTROL | FILE_READ_ATTRIBUTES, "host-only-DACL-management", management_error);
    require(valid(management.get()), "actual host can revoke the exact leaf DACL");
    const DWORD revoke_error = SetSecurityInfo(management.get(), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, revoked_leaf.dacl(), nullptr);
    Record("revoke_client_admission").number("error", revoke_error).boolean("client_job_complete", summary.client_natural_exit);
    require(revoke_error == ERROR_SUCCESS, "revoke native conditional and restricting SID grants");
    verify_dacl(management.get(), revoked_leaf, "after-admission-revocation");
    const auto revoked_identity = file_identity(management.get(), "same-object-after-admission-revocation");
    require(revoked_identity.volume == identity.volume && revoked_identity.id == identity.id &&
        revoked_identity.time == baseline_time, "revocation keeps exact physical leaf and pre-mutation time");
    summary.acl_revoked = true;
    auto after_denied = open_leaf(leaf, FILE_WRITE_ATTRIBUTES, "server-primary-WA-negative-after-revocation", denied_error);
    require(!valid(after_denied.get()) && denied_error == ERROR_ACCESS_DENIED, "fresh server WA still refused after revocation");
    require(management.close(), "management handle closes before retained-writer operations");
    for (DWORD index = 0; index < 4; ++index) {
        auto& result = summary.cases[index];
        if (valid(result.writer.get())) {
            no_thread_token("retained WA used under original server primary only");
            const std::uint64_t wanted = baseline_time + (static_cast<std::uint64_t>(index) + 1) * 10000000ULL;
            const FILETIME time{static_cast<DWORD>(wanted), static_cast<DWORD>(wanted >> 32)};
            const BOOL changed = SetFileTime(result.writer.get(), nullptr, nullptr, &time);
            const DWORD error = changed ? 0 : GetLastError();
            Record("post_completion_old_WA_mutation").number("case", index).boolean("ok", changed != FALSE)
                .number("error", error).text("writer_handle_decimal", handle_decimal(result.writer.get()))
                .text("requested_time_decimal", std::to_string(wanted)).boolean("client_natural_exit", summary.client_natural_exit)
                .boolean("acl_revoked", summary.acl_revoked);
            const auto observed = read_leaf(leaf, "independent-post-completion-mutation-readback");
            result.mutation_verified = changed && observed.volume == identity.volume && observed.id == identity.id && observed.time == wanted;
            require(result.mutation_verified, "pre-granted WA mutation and independent exact readback");
            require(result.writer.close(), "retained server WA closes after observation");
        }
        Record("case_final").number("case", index).boolean("client_opened", result.client_opened)
            .boolean("impersonated", result.impersonated).boolean("write_opened", result.write_opened)
            .boolean("post_completion_mutation_verified", result.mutation_verified);
    }
    summary.positive_control = summary.cases[3].client_opened && summary.cases[3].impersonated &&
        summary.cases[3].claim_values_match && summary.cases[3].write_opened && summary.cases[3].mutation_verified;
    require(summary.positive_control, "explicitly R-admitted duplex positive control completed end to end");
    summary.disqualified = summary.cases[0].mutation_verified || summary.cases[1].mutation_verified || summary.cases[2].mutation_verified;
    summary.write_connection_refused = !summary.cases[0].client_opened && summary.cases[0].open_error == ERROR_ACCESS_DENIED;
    require(summary.write_connection_refused || summary.cases[0].mutation_verified,
        "ordinary write-connection control refused or yielded its complete counterexample");
    // No refusal claim about other transports, other SQOS or actual cl/link.
    require(command_write.close() && report_read.close() && process.close() && job.close() && host_token.close(),
        "all remaining server-owned handles close");
}

DWORD WINAPI watchdog(LPVOID parameter) {
    const DWORD result = WaitForSingleObject(static_cast<HANDLE>(parameter), 30000);
    if (result != WAIT_OBJECT_0) {
        constexpr char message[] = "producer-delegation-006 watchdog expired or failed\n";
        DWORD written = 0;
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), message, sizeof(message) - 1, &written, nullptr);
        TerminateProcess(GetCurrentProcess(), 124);
    }
    return 0;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 6 && std::wcscmp(argv[1], L"--client") == 0) {
        emit_records = false;
        try { return run_client(argv[2], argv[3], parsed_handle(argv[4]), parsed_handle(argv[5])); }
        catch (...) { return 3; }
    }
    if (argc != 2) return 3;
    const auto started = GetTickCount64();
    Summary summary;
    bool complete = false;
    Handle stop(CreateEventW(nullptr, TRUE, FALSE, nullptr), "watchdog-stop");
    Handle watch;
    try {
        require(valid(stop.get()), "watchdog event created");
        watch = Handle(CreateThread(nullptr, 0, watchdog, stop.get(), 0, nullptr), "watchdog-thread");
        require(valid(watch.get()), "one auxiliary watchdog thread");
        Record("scope").text("attempt", "producer-delegation-006").number("max_processes", 2)
            .number("max_named_pipes", 8).number("protocol_channels", 2).number("cases", 4)
            .number("watchdog_seconds", 30).number("max_fixture_payload_bytes", 4096)
            .boolean("A1", false).boolean("A2", false).boolean("A4", false).boolean("deletion_enabled", false);
        run_server(argv[1], summary);
        complete = true;
    } catch (const std::exception& error) {
        ++errors;
        Record("exception").text("message", error.what());
    } catch (...) {
        ++errors; Record("exception").text("message", "unknown native exception");
    }
    if (valid(stop.get())) {
        const BOOL set = SetEvent(stop.get()); const DWORD error = set ? 0 : GetLastError();
        Record("watchdog_stop").boolean("ok", set != FALSE).number("error", error);
        if (!set) ++errors;
    }
    if (valid(watch.get())) {
        const DWORD wait = WaitForSingleObject(watch.get(), 5000);
        Record("watchdog_drain").number("result", wait).number("error", wait == WAIT_FAILED ? GetLastError() : 0);
        if (wait != WAIT_OBJECT_0) { ++errors; TerminateProcess(GetCurrentProcess(), 124); }
    }
    watch.close(); stop.close();
    // A failure may leave a case writer retained by the outer Summary. Close it
    // before classification so every close error is counted and summary is last.
    for (auto& result : summary.cases) result.writer.close();
    const bool interpretable = complete && errors == 0 && summary.cases_completed == 4 && summary.positive_control &&
        summary.client_natural_exit && summary.client_job_total == 1 && summary.client_job_active == 0 && summary.acl_revoked;
    const char* classification = !interpretable ? "INCONCLUSIVE" :
        (summary.disqualified ? "WRITE_RESTRICTED_IPC_DISQUALIFIED" : "FIXED_ROUTES_REFUSED_ONLY");
    const int exit_code = !interpretable ? 3 : (summary.disqualified ? 2 : 0);
    Record("summary").text("classification", classification).number("errors", errors)
        .number("cases_completed", summary.cases_completed).boolean("positive_control", summary.positive_control)
        .boolean("client_natural_exit", summary.client_natural_exit).number("client_job_total", summary.client_job_total)
        .number("client_job_active", summary.client_job_active).boolean("acl_revoked", summary.acl_revoked)
        .boolean("write_connection_refused", summary.write_connection_refused)
        .boolean("A1", false).boolean("A2", false).boolean("A4", false).boolean("A3", false).boolean("A5", false)
        .boolean("deletion_enabled", false).number("elapsed_ms_descriptive_only", GetTickCount64() - started);
    return exit_code;
}

