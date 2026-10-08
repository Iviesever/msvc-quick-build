#pragma once
// Diagnostic copies only. Never included by the product branches.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace mqb::pr247_diag {
enum Point : std::size_t {
    call_begin, runner_begin, create_begin, create_end, wait_begin, wait_end,
    join_begin, join_end, runner_return, call_end, observe_begin, observe_end, count
};
inline constexpr std::array<const char*, count> names{
    "call_begin", "runner_begin", "create_begin", "create_end", "wait_begin", "wait_end",
    "join_begin", "join_end", "runner_return", "call_end", "observe_begin", "observe_end"
};
struct Record {
    bool enabled{};
    std::wstring path;
    std::array<long long, count> ticks{};
    long long frequency{};
    DWORD pid{}, thread{}, child_pid{};
    FILETIME created{}, exited{}, kernel{}, user{};
    bool process_times_ok{};
};
inline thread_local Record record;
inline void mark(Point point) {
    if (!record.enabled) return;
    if (record.ticks[point] != 0) throw std::runtime_error("duplicate diagnostic boundary");
    const DWORD saved_error = ::GetLastError();
    LARGE_INTEGER value{};
    const bool ok = ::QueryPerformanceCounter(&value) != 0;
    ::SetLastError(saved_error);
    if (!ok) throw std::runtime_error("QPC failed");
    record.ticks[point] = value.QuadPart;
}
inline void begin() {
    record = {};
    std::array<wchar_t, 32768> path{};
    const DWORD length = ::GetEnvironmentVariableW(L"MQB_PR247_LINK_TRACE", path.data(),
                                                   static_cast<DWORD>(path.size()));
    if (length == 0) return;
    if (length >= path.size()) throw std::runtime_error("diagnostic path too long");
    record.path.assign(path.data(), length);
    LARGE_INTEGER frequency{};
    if (!::QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
        throw std::runtime_error("QPC frequency unavailable");
    record.frequency = frequency.QuadPart;
    record.pid = ::GetCurrentProcessId();
    record.thread = ::GetCurrentThreadId();
    record.enabled = true;
    mark(call_begin);
}
inline void retain_process_times(HANDLE process) {
    if (!record.enabled) return;
    record.child_pid = ::GetProcessId(process);
    record.process_times_ok = ::GetProcessTimes(process, &record.created, &record.exited,
                                               &record.kernel, &record.user) != 0;
}
inline unsigned long long value(FILETIME time) {
    return (static_cast<unsigned long long>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}
inline void write_new(const std::wstring& path, std::string_view bytes) {
    const HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                     CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("diagnostic CreateNew failed");
    DWORD written{};
    const bool ok = bytes.size() <= 0x7fffffff &&
        ::WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)
        && written == bytes.size();
    const bool closed = ::CloseHandle(file) != 0;
    if (!ok || !closed) throw std::runtime_error("diagnostic write incomplete");
}
inline void flush(std::string_view stdout_buffer, std::string_view stderr_buffer) {
    if (!record.enabled) return;
    // All serialization and I/O is AFTER the last measured boundary. Instrumented
    // whole-MQB time includes this observer: never subtract it to manufacture a score.
    std::string json = "{\"schema\":1,\"frequency\":" + std::to_string(record.frequency)
        + ",\"pid\":" + std::to_string(record.pid) + ",\"thread_id\":" + std::to_string(record.thread)
        + ",\"child_pid\":" + std::to_string(record.child_pid) + ",\"ticks\":{";
    for (std::size_t i = 0; i < count; ++i) {
        if (i) json += ',';
        json += '"'; json += names[i]; json += "\":" + std::to_string(record.ticks[i]);
    }
    json += "},\"process_times_ok\":" + std::string(record.process_times_ok ? "true" : "false")
        + ",\"creation_100ns\":" + std::to_string(value(record.created))
        + ",\"exit_100ns\":" + std::to_string(value(record.exited))
        + ",\"kernel_100ns\":" + std::to_string(value(record.kernel))
        + ",\"user_100ns\":" + std::to_string(value(record.user))
        + ",\"stdout_bytes\":" + std::to_string(stdout_buffer.size())
        + ",\"stderr_bytes\":" + std::to_string(stderr_buffer.size())
        + ",\"product_acceptance\":false,\"release_authorized\":false}";
    write_new(record.path + L".stdout", stdout_buffer);
    write_new(record.path + L".stderr", stderr_buffer);
    write_new(record.path, json);
    record.enabled = false;
}
} // namespace mqb::pr247_diag
