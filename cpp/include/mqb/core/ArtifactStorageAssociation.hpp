#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "mqb/core/BuildArtifactRecord.hpp"
#include "mqb/core/StorageInventory.hpp"

namespace mqb {

// Describes a reference in a past invocation, never present ownership.
enum class ArtifactPathRole { input, declared_output, metadata_reference };
enum class ArtifactStageKind { compile, pch, scan, link, archive };

struct ArtifactPathReference {
    std::filesystem::path path;
    ArtifactPathRole role;
};

struct ArtifactStorageStage {
    ArtifactStageKind kind;
    ArtifactCompletion completion;
    // Ordinary source records and scans do not expose a sealed cache outcome.
    std::optional<ArtifactCacheState> cache_state;
    // Known only where the original source association exposes this flag.
    std::optional<bool> source_has_warnings;
    std::optional<BuildConfiguration> configuration;
    std::optional<std::filesystem::path> working_directory;
    // Only an explicitly marked scan has this provenance. Not output ownership.
    bool toolchain_source{false};
    std::vector<ArtifactPathReference> paths;
};

struct ArtifactStorageReferences {
    std::optional<ArtifactGenerationLabel> caller_label;
    std::vector<ArtifactStorageStage> stages;
    static constexpr bool complete_reference_inventory = false;
};

enum class ArtifactPathObservation {
    unresolved_path,
    not_observed,       // NOT proof of absence, even if the walk had no issues
    observed_path,      // may be a directory, reparse point or unavailable row
    ambiguous_path,     // multiple observed rows share the supplied path key
};

struct ArtifactStorageMatch {
    std::size_t record_index{};
    std::size_t stage_index{};
    std::size_t path_index{};
    std::optional<std::filesystem::path> resolved_path;
    ArtifactPathObservation state{ArtifactPathObservation::unresolved_path};
    std::vector<std::size_t> observed_rows;
    // Other regular rows with the same observed ID, only after a unique path
    // match. This does NOT bind that ID to the historical invocation.
    std::vector<std::size_t> same_observed_file_rows;
    bool observed_identity_metadata_conflict{false};
};

struct ArtifactStorageAssociation {
    // Owned snapshots keep indices valid after the caller sorts or edits inputs.
    // No timestamp/currentness/atomic-snapshot guarantee is invented here.
    std::vector<ArtifactStorageReferences> records;
    StorageInventory observation;
    std::vector<ArtifactStorageMatch> matches;
    // Direct path matches only. Alias rows are reported separately above.
    // Repeated mentions and different records remain distinct; do not sum bytes.
    std::vector<std::vector<std::size_t>> row_matches;
    static constexpr bool invocation_identity_verified = false;
    static constexpr bool current_contents_verified = false;
    static constexpr bool deletion_authorized = false;
};

struct ArtifactStorageAssociationError {
    std::string message;
};

// Pure join: the supplied key must use the platform's existing lexical path
// authority without probing files. No filesystem calls, cache reads or writes.
// Relative record paths require their own recorded absolute cwd. Parent (..)
// traversal and NUL are refused rather than resolving through unknown aliases.
// Invalid inventory paths or an empty key fail the entire join. Exceptions
// from the supplied callback propagate; no partial success is manufactured.
// Rows, reference paths and expanded match/alias links each have a 1,000,000
// limit. Exceeding it rejects the result, never silently truncates coverage.
[[nodiscard]] std::expected<ArtifactStorageAssociation, ArtifactStorageAssociationError>
associate_artifact_storage(std::span<const ArtifactStorageReferences> records,
                           const StorageInventory& observation,
                           const StoragePathKey& key);

} // namespace mqb
