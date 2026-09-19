#include "mqb/platform/windows/StorageInventory.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <iomanip>
#include <sstream>
#include <utility>

namespace mqb::platform::windows {
namespace {
namespace fs = std::filesystem;
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    explicit Handle(HANDLE v = INVALID_HANDLE_VALUE) : value(v) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE) ::CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(std::exchange(other.value, INVALID_HANDLE_VALUE)) {}
};
struct FindHandle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~FindHandle() { if (value != INVALID_HANDLE_VALUE) ::FindClose(value); }
};
std::wstring native_path(const fs::path& path) {
    auto value = path.native();
    if (value.starts_with(L"\\\\?\\")) return value;
    if (value.starts_with(L"\\\\")) return L"\\\\?\\UNC\\" + value.substr(2);
    return L"\\\\?\\" + value;
}
Handle pin(const fs::path& path) {
    const auto name = native_path(path);
    return Handle{::CreateFileW(name.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
}
void issue(StorageInventory& result, const fs::path& path, const char* message, DWORD code = 0) {
    result.issues.push_back({path, message, code});
}
std::string physical_id(HANDLE handle) {
    FILE_ID_INFO id{};
    if (!::GetFileInformationByHandleEx(handle, FileIdInfo, &id, sizeof(id))) return {};
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << id.VolumeSerialNumber << ':';
    for (const auto byte : id.FileId.Identifier) out << std::setw(2) << static_cast<unsigned>(byte);
    return out.str();
}

class Walker {
public:
    StorageInventory result;
    const StorageFileObserver& observer;
    bool limit_reached{false};

    void walk(const fs::path& path, const fs::path& relative, unsigned depth) {
        if (limit_reached) return;
        if (result.entries.size() >= 1000000u) {
            issue(result, relative, "inventory entry limit (1000000) reached; partial result");
            limit_reached = true;
            return;
        }
        if (depth > 128u) {
            issue(result, relative, "inventory depth limit (128) reached; subtree not visited");
            return;
        }
        // Deny write and delete sharing for directories and files. Every
        // ancestor remains pinned on the recursion stack. This also refuses reparse
        // mutation through a write handle. Files deny writing
        // and replacement while metadata/legacy references are observed.
        const auto name = native_path(path);
        const DWORD attributes = ::GetFileAttributesW(name.c_str());
        const bool is_directory = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
        auto handle = pin(path);
        StorageEntry entry;
        entry.relative_path = relative;
        if (handle.value == INVALID_HANDLE_VALUE) {
            const DWORD code = ::GetLastError();
            if (relative.empty() && (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)) return;
            issue(result, relative, "cannot open entry without write/replacement ambiguity", code);
            if (!relative.empty()) result.entries.push_back(std::move(entry));
            return;
        }
        if (relative.empty()) result.root_exists = true;
        FILE_ATTRIBUTE_TAG_INFO tag{};
        if (!::GetFileInformationByHandleEx(handle.value, FileAttributeTagInfo, &tag, sizeof(tag))) {
            issue(result, relative, "entry attributes unavailable", ::GetLastError());
            result.entries.push_back(std::move(entry));
            return;
        }
        if (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            entry.kind = StorageEntryKind::reparse_point;
            result.entries.push_back(std::move(entry));
            issue(result, relative, "reparse point protected and not traversed");
            return;
        }
        const bool directory = (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (relative.empty() && !directory) {
            issue(result, relative, "artifact root is not a directory");
            return;
        }
        // A changed entry kind means enumeration and observation disagreed.
        // Preserve the refusal rather than treating this row as stable.
        if (directory != is_directory) {
            issue(result, relative, "entry kind changed during observation");
            result.entries.push_back(std::move(entry));
            return;
        }
        entry.kind = directory ? StorageEntryKind::directory : StorageEntryKind::file;
        if (!directory) {
            FILE_STANDARD_INFO standard{};
            const bool sizes_read = ::GetFileInformationByHandleEx(
                handle.value, FileStandardInfo, &standard, sizeof(standard)) != FALSE;
            const DWORD size_error = sizes_read ? 0 : ::GetLastError();
            if (sizes_read && !standard.DeletePending && !standard.Directory &&
                standard.EndOfFile.QuadPart >= 0 && standard.AllocationSize.QuadPart >= 0) {
                entry.logical_bytes = static_cast<std::uint64_t>(standard.EndOfFile.QuadPart);
                // Do not equate ordinary allocation accounting with physical
                // storage for compressed/sparse streams in this first slice.
                if (tag.FileAttributes & (FILE_ATTRIBUTE_COMPRESSED | FILE_ATTRIBUTE_SPARSE_FILE))
                    issue(result, relative, "compressed/sparse physical allocation not supported in this slice");
                else entry.allocated_bytes = static_cast<std::uint64_t>(standard.AllocationSize.QuadPart);
                entry.hard_links = standard.NumberOfLinks;
            } else issue(result, relative, "file size/allocation unavailable or delete pending", size_error);
            entry.physical_id = physical_id(handle.value);
            if (entry.physical_id.empty()) issue(result, relative, "physical file identity unavailable", ::GetLastError());
            if (observer) observer(path, entry, result);
            result.entries.push_back(std::move(entry));
            return;
        }
        if (!relative.empty()) result.entries.push_back(std::move(entry));
        WIN32_FIND_DATAW data{};
        const auto pattern = native_path(path / L"*");
        FindHandle found{::FindFirstFileW(pattern.c_str(), &data)};
        if (found.value == INVALID_HANDLE_VALUE) {
            const DWORD code = ::GetLastError();
            if (code != ERROR_FILE_NOT_FOUND) issue(result, relative, "directory enumeration failed", code);
            return;
        }
        do {
            const std::wstring_view filename{data.cFileName};
            if (filename == L"." || filename == L"..") continue;
            walk(path / data.cFileName, relative / data.cFileName, depth + 1u);
            if (limit_reached) return;
        } while (::FindNextFileW(found.value, &data));
        const DWORD code = ::GetLastError();
        if (code != ERROR_NO_MORE_FILES) issue(result, relative, "directory enumeration incomplete", code);
    }
};
} // namespace

StorageInventory scan_storage(const fs::path& artifact_root, const StorageFileObserver& observer) {
    Walker walker{{}, observer};
    walker.result.artifact_root = artifact_root.lexically_normal();
    if (!artifact_root.is_absolute() || artifact_root.native().find(L'\0') != std::wstring::npos) {
        issue(walker.result, {}, "inventory root must be an absolute path without NUL");
        return std::move(walker.result);
    }
    // Do not canonicalize away a junction inside .mqb. Pin each existing parent
    // first so a rename/reparse replacement cannot redirect subsequent paths.
    std::vector<Handle> ancestors;
    fs::path current = walker.result.artifact_root.root_path();
    auto pin_parent = [&](const fs::path& parent) {
        auto handle = pin(parent);
        if (handle.value == INVALID_HANDLE_VALUE) {
            issue(walker.result, parent, "cannot pin inventory ancestor", ::GetLastError());
            return false;
        }
        FILE_ATTRIBUTE_TAG_INFO tag{};
        if (!::GetFileInformationByHandleEx(handle.value, FileAttributeTagInfo, &tag, sizeof(tag)) ||
            !(tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            issue(walker.result, parent, "inventory ancestor is unavailable or a reparse point");
            return false;
        }
        ancestors.push_back(std::move(handle));
        return true;
    };
    if (!pin_parent(current)) return std::move(walker.result);
    const auto relative_parent = walker.result.artifact_root.parent_path().relative_path();
    for (const auto& component : relative_parent) {
        if (component == L"." || component == L"..") {
            issue(walker.result, current, "ambiguous inventory ancestor");
            return std::move(walker.result);
        }
        current /= component;
        if (!pin_parent(current)) return std::move(walker.result);
    }
    walker.walk(walker.result.artifact_root, {}, 0);
    return std::move(walker.result);
}
} // namespace mqb::platform::windows
