#pragma once

#include "mqb/core/LinkFactSnapshot.hpp"
#include <filesystem>

namespace mqb::platform::windows {
enum class LinkFactFileErrorCode {
    invalid_path, reparse_rejected, unavailable, already_exists, limit_exceeded,
    invalid_snapshot, transfer_failed, truncated, changed, flush_failed, close_failed
};
enum class LinkFactFileStage { input, ancestor, open, metadata, read, write, flush, close, decode };
struct LinkFactFileError {
    LinkFactFileErrorCode code;
    LinkFactFileStage stage;
    std::uint32_t native_code{}; // 0: policy/codec refusal, not an invented OS error.
    std::uint64_t transferred_bytes{}; // Known completed bytes, not a durability claim.
    bool file_created{false}; // A failed create may have left a NEW partial file; never deleted here.
    std::optional<std::uint32_t> close_error; // Cleanup failure does not replace primary error.
    std::optional<LinkFactSnapshotError> codec_error;
};
struct LinkFactFileReceipt {
    std::uint64_t document_bytes{};
    // Success means complete write + FlushFileBuffers + CloseHandle returned success.
    // NOT a power-loss guarantee, atomic publication protocol or authentication.
};

// Explicit single-file IO only. Ordinary absolute local-NTFS drive paths, pinned
// non-reparse ancestors. No default callers, cache/producer lookup or directory creation.
// CREATE_NEW never replaces an existing name. Failure retains its created prefix;
// no retry, unlink, rename, append, retirement or automatic reconciliation.
[[nodiscard]] std::expected<LinkFactFileReceipt, LinkFactFileError>
create_link_fact_snapshot_file(const std::filesystem::path& path, const LinkFactSnapshot& value);
// Existing leaf through one read handle, <=1 MiB, strict original codec. Shared
// read pins are temporary, not durable writer coordination or trusted current facts.
[[nodiscard]] std::expected<LinkFactSnapshot, LinkFactFileError>
read_link_fact_snapshot_file(const std::filesystem::path& path);
} // namespace mqb::platform::windows
