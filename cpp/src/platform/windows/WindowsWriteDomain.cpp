#include "mqb/platform/windows/WindowsWriteDomain.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winternl.h>

#include <algorithm>
#include <bit>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

namespace mqb::platform::windows {
namespace {
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~Handle() { if (valid()) ::CloseHandle(value); }
    bool valid() const noexcept { return value && value != INVALID_HANDLE_VALUE; }
};
WriteDomainError error(WriteDomainErrorCode code, const char* message, DWORD native = 0,
                      std::optional<std::uint32_t> status = {}) {
    return {code, message, native, status};
}
} // namespace

struct WindowsWriteDomain::State {
    // Reverse destruction order closes the marker before its pinned directory.
    Handle directory, marker;
    WriteDirectoryIdentity identity;
    WriteDomainPhase phase{WriteDomainPhase::pinned};
};
WindowsWriteDomain::WindowsWriteDomain(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}
WindowsWriteDomain::WindowsWriteDomain(WindowsWriteDomain&&) noexcept = default;
WindowsWriteDomain& WindowsWriteDomain::operator=(WindowsWriteDomain&&) noexcept = default;
WindowsWriteDomain::~WindowsWriteDomain() = default;

std::expected<WindowsWriteDomain, WriteDomainError>
WindowsWriteDomain::open(const std::filesystem::path& root) {
    if (!root.is_absolute() || root.native().find(L'\0') != std::wstring::npos)
        return std::unexpected(error(WriteDomainErrorCode::invalid_root, "write root must be an absolute existing directory"));
    auto state = std::make_unique<State>();
    // Resolve a junction to its actual directory. Deny deletion/renaming of that
    // directory while held; its ancestors can still change. Never canonicalize
    // then reopen a marker by string: try_reserve uses this HANDLE as its root.
    state->directory.value = ::CreateFileW(root.c_str(), FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY | SYNCHRONIZE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (!state->directory.valid())
        return std::unexpected(error(WriteDomainErrorCode::open_failed, "cannot pin write directory", ::GetLastError()));
    FILE_STANDARD_INFO standard{};
    if (!::GetFileInformationByHandleEx(state->directory.value, FileStandardInfo, &standard, sizeof(standard)))
        return std::unexpected(error(WriteDomainErrorCode::identity_failed, "directory attributes unavailable", ::GetLastError()));
    if (!standard.Directory)
        return std::unexpected(error(WriteDomainErrorCode::invalid_root, "write root is not a directory"));
    wchar_t filesystem[32]{};
    if (!::GetVolumeInformationByHandleW(state->directory.value, nullptr, 0, nullptr, nullptr, nullptr, filesystem, 32))
        return std::unexpected(error(WriteDomainErrorCode::unsupported_filesystem, "filesystem identity unavailable", ::GetLastError()));
    if (std::wstring_view{filesystem} != L"NTFS")
        return std::unexpected(error(WriteDomainErrorCode::unsupported_filesystem, "initial write-domain contract requires NTFS"));
    // A local volume-GUID name is required. Do not accept an SMB/UNC identity as
    // equivalent to a local file ID or silently fall back to a textual pathname.
    std::vector<wchar_t> final_name(32768);
    const auto n = ::GetFinalPathNameByHandleW(state->directory.value, final_name.data(),
        static_cast<DWORD>(final_name.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_GUID);
    if (!n || n >= final_name.size() || !std::wstring_view{final_name.data(), n}.starts_with(L"\\\\?\\Volume{"))
        return std::unexpected(error(WriteDomainErrorCode::unsupported_filesystem, "local volume-GUID identity required", n ? 0 : ::GetLastError()));
    FILE_ID_INFO identity{};
    if (!::GetFileInformationByHandleEx(state->directory.value, FileIdInfo, &identity, sizeof(identity)))
        return std::unexpected(error(WriteDomainErrorCode::identity_failed, "physical directory ID unavailable", ::GetLastError()));
    state->identity.volume = identity.VolumeSerialNumber;
    std::memcpy(state->identity.file.data(), identity.FileId.Identifier, state->identity.file.size());
    return WindowsWriteDomain{std::move(state)};
}
std::optional<WriteDirectoryIdentity> WindowsWriteDomain::identity() const noexcept {
    return state_ ? std::optional{state_->identity} : std::nullopt;
}
WriteDomainPhase WindowsWriteDomain::phase() const noexcept {
    return state_ ? state_->phase : WriteDomainPhase::empty;
}
std::expected<void, WriteDomainError> WindowsWriteDomain::try_reserve() {
    if (phase() != WriteDomainPhase::pinned)
        return std::unexpected(error(WriteDomainErrorCode::invalid_state, "reservation requires a pinned, unclaimed directory"));
    const auto module = ::GetModuleHandleW(L"ntdll.dll");
    if (!module)
        return std::unexpected(error(WriteDomainErrorCode::native_api_unavailable, "NT file API unavailable", ::GetLastError()));
    const auto entry = ::GetProcAddress(module, "NtCreateFile");
    if (!entry)
        return std::unexpected(error(WriteDomainErrorCode::native_api_unavailable, "NtCreateFile unavailable", ::GetLastError()));
    const auto create = std::bit_cast<decltype(&::NtCreateFile)>(entry);
    // Documented native file constants, deliberately not FILE_OPEN_IF. Neither an
    // existing empty file nor an existing symlink is opened, truncated or trusted.
    constexpr ULONG create_new = 2, synchronous = 0x20, not_directory = 0x40, open_reparse = 0x200000;
    UNICODE_STRING name{};
    name.Buffer = const_cast<PWSTR>(marker_name);
    name.Length = static_cast<USHORT>(sizeof(marker_name) - sizeof(wchar_t));
    name.MaximumLength = static_cast<USHORT>(sizeof(marker_name));
    OBJECT_ATTRIBUTES attributes{};
    attributes.Length = sizeof(attributes);
    attributes.RootDirectory = state_->directory.value;
    attributes.ObjectName = &name;
    // Exact fixed component spelling, no path traversal and no inherited handle.
    IO_STATUS_BLOCK io{};
    HANDLE marker{INVALID_HANDLE_VALUE};
    const auto status = create(&marker, FILE_WRITE_DATA | FILE_READ_ATTRIBUTES | DELETE | SYNCHRONIZE,
        &attributes, &io, nullptr, FILE_ATTRIBUTE_NORMAL, 0, create_new,
        synchronous | not_directory | open_reparse, nullptr, 0);
    if (status < 0) {
        constexpr auto collision = static_cast<NTSTATUS>(0xc0000035UL);
        constexpr auto sharing = static_cast<NTSTATUS>(0xc0000043UL);
        return std::unexpected(error(status == collision || status == sharing
            ? WriteDomainErrorCode::occupied_or_unresolved : WriteDomainErrorCode::claim_failed,
            "write marker is occupied, unresolved or inaccessible; no automatic takeover", 0,
            static_cast<std::uint32_t>(status)));
    }
    state_->marker.value = marker;
    if (status != 0 || io.Information != 2 || !state_->marker.valid()) {
        state_->phase = WriteDomainPhase::quarantined;
        return std::unexpected(error(WriteDomainErrorCode::claim_failed,
            "native create did not confirm a fresh marker", 0, static_cast<std::uint32_t>(status)));
    }
    state_->phase = WriteDomainPhase::quarantined; // Errors/exceptions preserve the on-disk marker.
    // Content is diagnostic only. Exclusivity comes from atomic FILE_CREATE at
    // the pinned physical directory; no stale PID/time/content is an authority.
    constexpr char record[] = "MQB-WRITE-DOMAIN-v1\nunresolved\n";
    DWORD written{};
    if (!::WriteFile(marker, record, sizeof(record) - 1, &written, nullptr))
        return std::unexpected(error(WriteDomainErrorCode::marker_io_failed, "cannot write unresolved marker", ::GetLastError()));
    if (written != sizeof(record) - 1)
        return std::unexpected(error(WriteDomainErrorCode::marker_io_failed, "incomplete unresolved marker", ERROR_WRITE_FAULT));
    if (!::FlushFileBuffers(marker))
        return std::unexpected(error(WriteDomainErrorCode::marker_io_failed, "cannot flush unresolved marker", ::GetLastError()));
    // This flush does not claim namespace persistence across power loss. Process
    // death preserves the marker; storage/volume resets are outside this version.
    state_->phase = WriteDomainPhase::reserved;
    return {};
}
std::expected<void, WriteDomainError> WindowsWriteDomain::begin_writes() {
    if (phase() != WriteDomainPhase::reserved)
        return std::unexpected(error(WriteDomainErrorCode::invalid_state, "writes require an unstarted reservation"));
    state_->phase = WriteDomainPhase::writing;
    return {};
}
std::expected<void, WriteDomainError> WindowsWriteDomain::withdraw_unstarted() {
    if (phase() == WriteDomainPhase::writing)
        return std::unexpected(error(WriteDomainErrorCode::writes_started, "writer completion is unproven; keep unresolved marker"));
    if (phase() != WriteDomainPhase::reserved)
        return std::unexpected(error(WriteDomainErrorCode::invalid_state, "only an unstarted reservation can be withdrawn"));
    state_->phase = WriteDomainPhase::quarantined;
    FILE_DISPOSITION_INFO disposition{TRUE};
    // Delete ONLY the marker HANDLE we created, never a looked-up pathname.
    if (!::SetFileInformationByHandle(state_->marker.value, FileDispositionInfo, &disposition, sizeof(disposition)))
        return std::unexpected(error(WriteDomainErrorCode::withdrawal_failed, "unstarted marker withdrawal failed", ::GetLastError()));
    if (!::CloseHandle(state_->marker.value))
        return std::unexpected(error(WriteDomainErrorCode::withdrawal_failed, "unstarted marker close failed", ::GetLastError()));
    state_->marker.value = INVALID_HANDLE_VALUE;
    state_->phase = WriteDomainPhase::pinned;
    return {};
}
} // namespace mqb::platform::windows
