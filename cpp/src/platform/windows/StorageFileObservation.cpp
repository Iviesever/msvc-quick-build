#include "mqb/platform/windows/StorageFileObservation.hpp"
#include "PhysicalPath.hpp"
#include "StorageReadPrimitives.hpp"

#include <string_view>
#include <utility>
#include <vector>

namespace mqb::platform::windows {
namespace fs = std::filesystem;
using detail::Handle;
using detail::pin;
using detail::physical_id;

StorageFileObservation observe_storage_file(const fs::path& requested) {
    using State = StorageFileObservationState;
    StorageFileObservation out;
    out.requested_path = requested;
    auto refuse = [&](State state, const fs::path& path, const char* message, DWORD code = 0) {
        out.state = state;
        out.issues.push_back({path, message, code});
    };
    // Validate BEFORE normalization, so '..' cannot erase a reparse ancestor.
    if (!detail::physical_path_supported(requested, WriteExtent::file)) {
        refuse(State::invalid_path, requested, "unambiguous absolute drive file path required; no device/ADS/parent traversal");
        return out;
    }
    if (requested.native().size() > StorageFileObservation::maximum_path_characters) {
        refuse(State::limit_exceeded, requested, "file observation path length limit exceeded");
        return out;
    }
    const auto path = requested.lexically_normal();
    std::size_t depth = 0;
    for ([[maybe_unused]] const auto& component : path.relative_path())
        if (++depth > StorageFileObservation::maximum_depth) {
            refuse(State::limit_exceeded, requested, "file observation depth limit exceeded before opening");
            return out;
        }
    std::vector<Handle> ancestors;
    ancestors.reserve(depth);
    auto parent = path.root_path();
    auto pin_parent = [&](const fs::path& current) {
        auto handle = pin(current); // Same read-data/read-attributes share boundary as scan_storage.
        if (handle.value == INVALID_HANDLE_VALUE) {
            refuse(State::unavailable, current, "file observation ancestor unavailable", ::GetLastError());
            return false; // A missing parent is not a missing-leaf observation.
        }
        ++out.opened_components;
        FILE_ATTRIBUTE_TAG_INFO tag{};
        if (!::GetFileInformationByHandleEx(handle.value, FileAttributeTagInfo, &tag, sizeof(tag))) {
            refuse(State::unavailable, current, "file observation ancestor attributes unavailable", ::GetLastError());
            return false;
        }
        if (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            refuse(State::reparse_rejected, current, "file observation reparse ancestor rejected");
            return false;
        }
        if (!(tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            refuse(State::unavailable, current, "file observation ancestor is not a directory");
            return false;
        }
        ancestors.push_back(std::move(handle));
        return true;
    };
    if (!pin_parent(parent)) return out;
    for (const auto& component : path.parent_path().relative_path()) {
        parent /= component;
        if (!pin_parent(parent)) return out;
    }
    auto file = pin(path);
    if (file.value == INVALID_HANDLE_VALUE) {
        const auto code = ::GetLastError();
        refuse(code == ERROR_FILE_NOT_FOUND ? State::missing_leaf : State::unavailable,
            path, "file observation leaf could not be opened", code);
        return out;
    }
    ++out.opened_components;
    FILE_ATTRIBUTE_TAG_INFO tag{};
    if (!::GetFileInformationByHandleEx(file.value, FileAttributeTagInfo, &tag, sizeof(tag))) {
        refuse(State::unavailable, path, "file observation attributes unavailable", ::GetLastError());
        return out;
    }
    if (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        refuse(State::reparse_rejected, path, "file observation final reparse point rejected");
        return out;
    }
    if (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        refuse(State::not_regular_file, path, "file observation refuses a directory");
        return out;
    }
    // Keep the initial file-ID scope aligned with the existing write-domain
    // local-NTFS restriction, without using its reservation or marker API.
    wchar_t filesystem[32]{};
    if (!::GetVolumeInformationByHandleW(file.value, nullptr, 0, nullptr, nullptr, nullptr, filesystem, 32)) {
        refuse(State::unavailable, path, "file observation filesystem unavailable", ::GetLastError());
        return out;
    }
    std::vector<wchar_t> final_name(32768);
    const auto n = ::GetFinalPathNameByHandleW(file.value, final_name.data(),
        static_cast<DWORD>(final_name.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_GUID);
    if (std::wstring_view{filesystem} != L"NTFS" || !n || n >= final_name.size() ||
        !std::wstring_view{final_name.data(), n}.starts_with(L"\\\\?\\Volume{")) {
        refuse(State::unavailable, path, "file observation requires local volume-GUID NTFS identity", n ? 0 : ::GetLastError());
        return out;
    }
    FILE_STANDARD_INFO standard{};
    if (!::GetFileInformationByHandleEx(file.value, FileStandardInfo, &standard, sizeof(standard))) {
        refuse(State::unavailable, path, "file observation sizes unavailable", ::GetLastError());
        return out;
    }
    if (standard.DeletePending || standard.Directory || standard.EndOfFile.QuadPart < 0 ||
        standard.AllocationSize.QuadPart < 0 || standard.NumberOfLinks == 0) {
        refuse(State::unavailable, path, "file observation has invalid or delete-pending metadata");
        return out;
    }
    auto id = physical_id(file.value);
    if (id.empty()) {
        refuse(State::unavailable, path, "file observation identity unavailable", ::GetLastError());
        return out;
    }
    out.physical_id = std::move(id);
    out.logical_bytes = static_cast<std::uint64_t>(standard.EndOfFile.QuadPart);
    out.hard_links = standard.NumberOfLinks; // Multiple names do not invalidate an observation or imply exclusivity.
    if (tag.FileAttributes & (FILE_ATTRIBUTE_COMPRESSED | FILE_ATTRIBUTE_SPARSE_FILE))
        out.issues.push_back({path, "compressed/sparse physical allocation not supported in this slice", 0});
    else out.allocated_bytes = static_cast<std::uint64_t>(standard.AllocationSize.QuadPart);
    out.state = State::observed;
    return out; // RAII releases leaf and ancestors, including all early returns.
}
} // namespace mqb::platform::windows
