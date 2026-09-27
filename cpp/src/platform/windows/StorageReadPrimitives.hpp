#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <iomanip>
#include <sstream>
#include <utility>

// Verbatim read-handle, path, pin and identity bodies extracted from the
// existing inventory. Shared private implementation, not a second IO policy.
namespace mqb::platform::windows::detail {
namespace fs = std::filesystem;
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    explicit Handle(HANDLE v = INVALID_HANDLE_VALUE) : value(v) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE) ::CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(std::exchange(other.value, INVALID_HANDLE_VALUE)) {}
};
inline std::wstring native_path(const fs::path& path) {
    auto value = path.native();
    if (value.starts_with(L"\\\\?\\")) return value;
    if (value.starts_with(L"\\\\")) return L"\\\\?\\UNC\\" + value.substr(2);
    return L"\\\\?\\" + value;
}
inline Handle pin(const fs::path& path) {
    const auto name = native_path(path);
    // Attribute-only access does not participate in read/write/delete share
    // checking. Request data access (FILE_LIST_DIRECTORY on directories) too,
    // so FILE_SHARE_READ actually excludes a concurrent writer or renamer.
    // Do not fall back to an attribute-only handle on permission failure.
    return Handle{::CreateFileW(name.c_str(), FILE_READ_DATA | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
}
inline std::string physical_id(HANDLE handle) {
    FILE_ID_INFO id{};
    if (!::GetFileInformationByHandleEx(handle, FileIdInfo, &id, sizeof(id))) return {};
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << id.VolumeSerialNumber << ':';
    for (const auto byte : id.FileId.Identifier) out << std::setw(2) << static_cast<unsigned>(byte);
    return out.str();
}

} // namespace mqb::platform::windows::detail
