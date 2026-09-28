#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mqb/core/BuildArtifactRecord.hpp"
#include "mqb/core/StorageFileObservation.hpp"

namespace mqb {

enum class LinkFactWarningCode { cache_load_failed, cache_save_failed, file_snapshot_failed };
struct LinkFactWarning {
    LinkFactWarningCode code{LinkFactWarningCode::file_snapshot_failed};
    std::string path;
    std::string message;
    bool operator==(const LinkFactWarning&) const = default;
};
struct LinkFactIssue {
    std::string path;
    std::string message;
    std::uint32_t native_code{};
    bool operator==(const LinkFactIssue&) const = default;
};
struct LinkFactObservation {
    bool observer_invoked{false};
    std::string requested_path;
    StorageFileObservationState state{StorageFileObservationState::not_attempted};
    std::string physical_id;
    std::optional<std::uint64_t> logical_bytes;
    std::optional<std::uint64_t> allocated_bytes;
    std::optional<std::uint32_t> hard_links;
    std::uint32_t opened_components{};
    std::vector<LinkFactIssue> issues;
    bool operator==(const LinkFactObservation&) const = default;
};

// Owned, explicitly UNVERIFIED historical projection, not a cache entry/recipe.
// Paths are lossless UTF-8 labels, never resolved, normalized, or opened here.
// No objects/libraries/side-output inventory, file contents, or writer lease.
struct LinkFactSnapshot {
    std::optional<std::string> capture_label; // Caller annotation, NOT an atomic timestamp.
    ArtifactCompletion completion{ArtifactCompletion::reused};
    ArtifactCacheState cache_state{ArtifactCacheState::reused};
    bool linked{false};
    std::string output;
    std::string cache_file;
    std::optional<std::string> working_directory;
    SignatureDigest signature; // Opaque reference to the same record, not recomputed.
    std::string linker_path;
    std::string linker_version;
    std::string linker_stamp;
    BuildConfiguration configuration{BuildConfiguration::debug};
    Architecture architecture{Architecture::x64};
    TargetKind target_kind{TargetKind::executable};
    std::vector<LinkFactWarning> warnings;
    LinkFactObservation observation;

    static constexpr bool producer_identity_verified = false;
    static constexpr bool current_content_verified = false;
    static constexpr bool complete_producer_inventory = false;
    static constexpr bool deletion_authorized = false;
    bool operator==(const LinkFactSnapshot&) const = default;
};

struct LinkFactSnapshotLimits {
    static constexpr std::size_t document_bytes = 1024 * 1024;
    static constexpr std::size_t string_bytes = 128 * 1024;
    static constexpr std::size_t total_string_bytes = 512 * 1024;
    static constexpr std::size_t warnings = 64;
    static constexpr std::size_t issues = 128;
    static constexpr std::size_t nesting = 12;
    static constexpr std::size_t structural_tokens = 4096;
};
enum class LinkFactSnapshotErrorCode { invalid_document, invalid_record, unsupported_version, limit_exceeded };
struct LinkFactSnapshotError {
    LinkFactSnapshotErrorCode code;
    std::string message;
};

// Pure bounded codec, one schema v1. Unknown/missing fields, duplicate decoded
// keys, invalid UTF-8, NUL, lossy/negative/fractional integers, and true authority
// flags are refused. No file IO, cache rehydration, clock read, or authentication.
[[nodiscard]] std::expected<void, LinkFactSnapshotError>
validate_link_fact_snapshot(const LinkFactSnapshot& value);
[[nodiscard]] std::expected<std::string, LinkFactSnapshotError>
encode_link_fact_snapshot(const LinkFactSnapshot& value);
[[nodiscard]] std::expected<LinkFactSnapshot, LinkFactSnapshotError>
decode_link_fact_snapshot(std::string_view text);

} // namespace mqb
