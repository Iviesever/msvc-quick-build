#include "mqb/platform/windows/LinkFactSnapshotFile.hpp"
#include "LinkFactFileTransfer.hpp"
#include "PhysicalPath.hpp"
#include "StorageReadPrimitives.hpp"

#include <vector>

namespace mqb::platform::windows {
namespace fs = std::filesystem;
namespace ff = detail::fact_file;
namespace {
using Code = LinkFactFileErrorCode;
using Stage = LinkFactFileStage;
using detail::Handle;

bool utf16_path(std::wstring_view text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned>(text[i]);
        if (c >= 0xdc00 && c <= 0xdfff) return false;
        if (c >= 0xd800 && c <= 0xdbff) {
            if (++i == text.size() || text[i] < 0xdc00 || text[i] > 0xdfff) return false;
        }
    }
    return true;
}
std::expected<void, LinkFactFileError> inspect(HANDLE h, bool directory, Stage stage) {
    FILE_ATTRIBUTE_TAG_INFO tag{};
    if (!::GetFileInformationByHandleEx(h, FileAttributeTagInfo, &tag, sizeof(tag)))
        return std::unexpected(ff::error(Code::unavailable, stage, ::GetLastError()));
    if (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        return std::unexpected(ff::error(Code::reparse_rejected, stage));
    if (!!(tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory || ::GetFileType(h) != FILE_TYPE_DISK)
        return std::unexpected(ff::error(Code::unavailable, stage));
    return {};
}
std::expected<std::vector<Handle>, LinkFactFileError> parents(const fs::path& path) {
    if (!utf16_path(path.native()) || !detail::physical_path_supported(path, WriteExtent::file))
        return std::unexpected(ff::error(Code::invalid_path, Stage::input));
    if (path.native().size() > StorageFileObservation::maximum_path_characters)
        return std::unexpected(ff::error(Code::limit_exceeded, Stage::input));
    std::size_t depth = 0;
    for ([[maybe_unused]] const auto& c : path.relative_path())
        if (++depth > StorageFileObservation::maximum_depth)
            return std::unexpected(ff::error(Code::limit_exceeded, Stage::input));
    std::vector<Handle> pins;
    pins.reserve(depth + 1);
    auto current = path.root_path();
    auto add = [&]() -> std::expected<void, LinkFactFileError> {
        auto h = detail::pin(current);
        if (h.value == INVALID_HANDLE_VALUE)
            return std::unexpected(ff::error(Code::unavailable, Stage::ancestor, ::GetLastError()));
        if (auto checked = inspect(h.value, true, Stage::ancestor); !checked) return checked;
        pins.push_back(std::move(h));
        return {};
    };
    if (auto ok = add(); !ok) return std::unexpected(ok.error());
    wchar_t filesystem[32]{};
    std::vector<wchar_t> name(32768);
    if (!::GetVolumeInformationByHandleW(pins.front().value, nullptr, 0, nullptr, nullptr, nullptr, filesystem, 32))
        return std::unexpected(ff::error(Code::unavailable, Stage::ancestor, ::GetLastError()));
    const auto n = ::GetFinalPathNameByHandleW(pins.front().value, name.data(),
        static_cast<DWORD>(name.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_GUID);
    if (!n) return std::unexpected(ff::error(Code::unavailable, Stage::ancestor, ::GetLastError()));
    if (std::wstring_view{filesystem} != L"NTFS" || n >= name.size() ||
        !std::wstring_view{name.data(), n}.starts_with(L"\\\\?\\Volume{"))
        return std::unexpected(ff::error(Code::unavailable, Stage::ancestor));
    for (const auto& c : path.lexically_normal().parent_path().relative_path()) {
        current /= c;
        if (auto ok = add(); !ok) return std::unexpected(ok.error());
    }
    return pins; // Move-only read pins live until this operation returns.
}
struct Transport {
    Handle handle;
    explicit Transport(Handle h) : handle(std::move(h)) {}
    std::expected<std::size_t, std::uint32_t> write(std::span<const char> bytes) {
        DWORD done{};
        if (!::WriteFile(handle.value, bytes.data(), static_cast<DWORD>(bytes.size()), &done, nullptr))
            return std::unexpected(::GetLastError());
        return done;
    }
    std::expected<std::size_t, std::uint32_t> read(std::span<char> bytes) {
        DWORD done{};
        if (!::ReadFile(handle.value, bytes.data(), static_cast<DWORD>(bytes.size()), &done, nullptr))
            return std::unexpected(::GetLastError());
        return done;
    }
    std::expected<std::uint64_t, std::uint32_t> size() {
        LARGE_INTEGER value{};
        if (!::GetFileSizeEx(handle.value, &value)) return std::unexpected(::GetLastError());
        if (value.QuadPart < 0) return std::unexpected(static_cast<std::uint32_t>(ERROR_INVALID_DATA));
        return static_cast<std::uint64_t>(value.QuadPart);
    }
    std::expected<void, std::uint32_t> flush() {
        if (!::FlushFileBuffers(handle.value)) return std::unexpected(::GetLastError());
        return {};
    }
    std::expected<void, std::uint32_t> close() {
        const auto h = std::exchange(handle.value, INVALID_HANDLE_VALUE);
        if (!::CloseHandle(h)) return std::unexpected(::GetLastError());
        return {};
    }
};
} // namespace

std::expected<LinkFactFileReceipt, LinkFactFileError>
create_link_fact_snapshot_file(const fs::path& path, const LinkFactSnapshot& value) {
    const auto text = encode_link_fact_snapshot(value); // Invalid input never creates an empty file.
    if (!text) {
        auto e = ff::error(Code::invalid_snapshot, Stage::input); e.codec_error = text.error();
        return std::unexpected(std::move(e));
    }
    auto pins = parents(path);
    if (!pins) return std::unexpected(pins.error());
    const auto native = detail::native_path(path.lexically_normal());
    Transport io{Handle{::CreateFileW(native.c_str(), GENERIC_WRITE | FILE_READ_ATTRIBUTES, 0,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)}};
    if (io.handle.value == INVALID_HANDLE_VALUE) {
        const auto code = ::GetLastError();
        return std::unexpected(ff::error(code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS ?
            Code::already_exists : Code::unavailable, Stage::open, code));
    }
    if (auto checked = inspect(io.handle.value, false, Stage::metadata); !checked) {
        auto e = checked.error(); e.file_created = true;
        return ff::finish<LinkFactFileReceipt>(io, std::unexpected(e), 0, true);
    }
    return ff::write_document(io, *text);
}
std::expected<LinkFactSnapshot, LinkFactFileError> read_link_fact_snapshot_file(const fs::path& path) {
    auto pins = parents(path);
    if (!pins) return std::unexpected(pins.error());
    Transport io{detail::pin(path.lexically_normal())};
    if (io.handle.value == INVALID_HANDLE_VALUE)
        return std::unexpected(ff::error(Code::unavailable, Stage::open, ::GetLastError()));
    if (auto checked = inspect(io.handle.value, false, Stage::metadata); !checked)
        return ff::finish<LinkFactSnapshot>(io, std::unexpected(checked.error()), 0, false);
    auto text = ff::read_document(io);
    if (!text) return std::unexpected(std::move(text.error()));
    auto value = decode_link_fact_snapshot(*text);
    if (!value) {
        auto e = ff::error(Code::invalid_snapshot, Stage::decode, 0, text->size());
        e.codec_error = value.error();
        return std::unexpected(std::move(e));
    }
    return std::move(*value);
}
} // namespace mqb::platform::windows
