#pragma once

#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "mqb/core/LinkFactSnapshot.hpp"
#include "mqb/core/ToolchainIdentity.hpp"
#include "mqb/orchestration/ArtifactStorageProjection.hpp"
#include "mqb/orchestration/MsvcModuleTargetCoordinator.hpp"

namespace mqb::orchestration {
// Explicit caller identity claims, not labels parsed from paths or authenticated
// writers. Project and target are compared as opaque, case-sensitive strings.
struct ArtifactTargetKey {
    std::string project;
    std::string target;
    bool operator==(const ArtifactTargetKey&) const = default;
};
struct ArtifactGenerationKey {
    ArtifactTargetKey target;
    std::string generation;
    bool operator==(const ArtifactGenerationKey&) const = default;
};
using GenerationTargetRecord = std::variant<TargetArtifactRecord, StaticTargetArtifactRecord,
                                            ModuleTargetArtifactRecord>;
struct ArtifactGenerationInput {
    std::string source_id; // Unique historical record locator in this batch, NOT authentication.
    ArtifactTargetKey target;
    // executed: caller's generation claim; reused: explicit reference to an
    // executed generation in this batch. Missing values are never inferred.
    std::optional<std::string> generation;
    // The old target records do not capture the compiler identity. The caller
    // must supply its original context or leave it unknown, never reconstruct it.
    std::optional<ToolchainIdentity> compiler;
    GenerationTargetRecord record;
    std::optional<LinkFactSnapshot> snapshot; // Optional link-main-output history only.
};
enum class ArtifactGenerationIssue {
    missing_generation, missing_compiler, duplicate_source, duplicate_generation,
    missing_origin, ambiguous_origin, recipe_mismatch, mixed_reuse,
    invalid_completion, invalid_recipe, snapshot_mismatch, unresolved_path,
};
enum class ArtifactGenerationState { executed_claim, explicit_reuse, unresolved, conflicting };
struct ArtifactGenerationRecord {
    std::string source_id;
    ArtifactTargetKey target;
    std::optional<std::string> generation;
    ArtifactCompletion completion{ArtifactCompletion::reused};
    std::optional<ArtifactGenerationLabel> caller_label;
    std::optional<LinkFactSnapshot> snapshot;
    // Length-framed exact captured fields, NOT a new cache signature or a full
    // content/environment identity. Options keep their original order/spelling.
    std::string recipe_evidence;
    ArtifactGenerationState state{ArtifactGenerationState::unresolved};
    std::optional<std::size_t> origin_record;
    std::vector<ArtifactGenerationIssue> issues;
};
enum class ArtifactRetentionState { selected, missing, ambiguous, unresolved };
struct ArtifactRetentionSelection {
    ArtifactGenerationKey request;
    ArtifactRetentionState state{ArtifactRetentionState::missing};
    std::vector<std::size_t> executed_candidates;
    std::vector<std::size_t> associated_records;
};
struct ArtifactGenerationPathGroup {
    std::string path_key;
    std::vector<std::size_t> matches; // Indices into references.matches; roles preserved.
    std::vector<std::size_t> records;
    std::vector<std::size_t> retained_records;
    bool shared_reference{false};
    bool multiple_executed_generations{false}; // Potential overwrite, not a current-file fact.
    bool different_target_or_recipe{false};
    bool unresolved_members{false};
};
struct ArtifactGenerationModel {
    std::vector<ArtifactGenerationRecord> records;
    ArtifactStorageAssociation references; // Owns all selected typed projections and paths.
    std::vector<ArtifactRetentionSelection> retention;
    std::vector<ArtifactGenerationPathGroup> paths;
    static constexpr bool producer_identity_verified = false;
    static constexpr bool current_content_verified = false;
    static constexpr bool complete_producer_inventory = false;
    static constexpr bool deletion_authorized = false;
};
struct ArtifactGenerationLimits {
    static constexpr std::size_t records = 256;
    static constexpr std::size_t stages = 4096;
    static constexpr std::size_t paths = 65536;
    static constexpr std::size_t items = 65536;
    static constexpr std::size_t text_bytes = 8 * 1024 * 1024;
};
struct ArtifactGenerationError { std::string message; };

// Pure batch model. No IO, clock, build, scan, callback observer or persistence.
// The root/key have the SAME lexical-only contract as associate_artifact_storage;
// an empty inventory is used, never to resolve stage paths from another cwd.
// Invalid keys/limits fail the whole request; absent/contradictory historical
// evidence stays visible as issues. No partial output on exceptions. A retention
// request never authorizes deletion of unselected, unresolved or absent records.
[[nodiscard]] std::expected<ArtifactGenerationModel, ArtifactGenerationError>
model_artifact_generations(std::span<const ArtifactGenerationInput> records,
                           std::span<const ArtifactGenerationKey> retain,
                           const std::filesystem::path& lexical_root,
                           const StoragePathKey& key);

// BEGIN recorded generation model interface
// This short-lived input borrows a completed invocation so that admission does
// not copy its process output, environment, or cache vectors before budgeting.
// Keep each referenced invocation alive and unchanged for the synchronous call.
struct RecordedArtifactGenerationInput {
    std::string source_id;
    ArtifactTargetKey target;
    std::optional<std::string> generation;
    std::variant<std::reference_wrapper<const RecordedTargetResult>,
                 std::reference_wrapper<const RecordedStaticTargetResult>> record;
    std::optional<LinkFactSnapshot> snapshot;
};
struct RecordedArtifactGenerationModel {
    ArtifactGenerationModel model;
    // compiles[record_index][source_index] owns the strictly projected context.
    // Its corresponding compile stage is model.references.records[i].stages[j].
    std::vector<std::vector<CompileStorageContext>> compiles;
    static constexpr bool producer_identity_verified = false;
    static constexpr bool current_content_verified = false;
    static constexpr bool complete_producer_inventory = false;
    static constexpr bool deletion_authorized = false;
};
struct RecordedArtifactGenerationError {
    std::string message;
    std::optional<std::size_t> record_index;
    // Preserve the strict projector's issue, source slot and diagnostic.
    std::optional<RecordedStorageProjectionError> projection_error;
};

// Ordinary EXE/DLL/static only. All selected input fields and projected shape
// are preflighted for the whole batch against the model/projection intersection
// before owning result copies. These are traversal/copy bounds, not an RSS cap.
// Uses captured per-source inspection AND cache identities/signatures, never a
// caller-wide replacement compiler. Outcomes, force and warnings are retained,
// but are not recipe identity. Old APIs/module unknown semantics are unchanged.
// No IO, current-toolchain lookup, callback observer, recompile or authority gain.
// Exceptions (including allocation and the pure lexical key) propagate.
[[nodiscard]] std::expected<RecordedArtifactGenerationModel, RecordedArtifactGenerationError>
model_recorded_artifact_generations(std::span<const RecordedArtifactGenerationInput> records,
                                   std::span<const ArtifactGenerationKey> retain,
                                   const std::filesystem::path& lexical_root,
                                   const StoragePathKey& key);
// END recorded generation model interface
} // namespace mqb::orchestration
