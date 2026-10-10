#pragma once

#include <cstddef>
#include <expected>
#include <string_view>

#include "mqb/orchestration/ArtifactGenerationModel.hpp"
#include "mqb/orchestration/MsvcIncrementalStaticTargetCoordinator.hpp"

namespace mqb::orchestration {

// Selected, UNAUTHENTICATED completion claims. These are deliberately separate
// from Recorded*Result: no ProcessResult/Spec, environment, plan, timings or
// executable recovery command can be constructed by the decoder. The member
// shape permits the same strict pure validators to consume live and archived
// evidence, without manufacturing a successful invocation.
struct ArchivedCompileCompletion {
    bool compiled{false};
    std::vector<IncrementalCompileWarning> warnings;
};
struct ArchivedSourceCompletion {
    std::filesystem::path source;
    ArchivedCompileCompletion result;
};
struct ArchivedInspectionToolchain {
    ToolchainIdentity identity;
};
struct ArchivedCompileEvidence {
    IncrementalCompileRequest request;
    ArchivedInspectionToolchain inspection_toolchain;
    CompileCacheEntry cache_entry{.source={},.kind=TranslationUnitKind::source,.toolchain={},
        .signature=BuildSignature::from_digest({}),.outputs={},.dependencies={},
        .include_search_roots={},.module_scan={}};
    CompileCacheEvidenceState state{CompileCacheEvidenceState::reused};
    std::optional<CompileCacheFileError> save_error;
};
struct ArchivedCompileWave {
    std::vector<ArchivedCompileEvidence> compiles;
};
struct ArchivedLinkCompletion {
    bool linked{false};
    std::vector<IncrementalLinkWarning> warnings;
};
struct ArchivedArchiveCompletion {
    bool archived{false};
    std::vector<IncrementalArchiveWarning> warnings;
};
struct ArchivedTargetCompletion {
    std::vector<ArchivedSourceCompletion> compiles;
    ArchivedLinkCompletion link;
    bool any_compiled{false};
};
struct ArchivedStaticTargetCompletion {
    std::vector<ArchivedSourceCompletion> compiles;
    ArchivedArchiveCompletion archive;
    bool any_compiled{false};
};
struct ArchivedTargetClaim {
    ArchivedTargetCompletion result;
    TargetArtifactRecord record{.caller_label={},.compiler_options={},.sources={},.additional_object_inputs={},
        .link={.completion=ArtifactCompletion::reused,.cache_state=ArtifactCacheState::reused,
            .association={.linker={},.signature=BuildSignature::from_digest({}),.objects={},.output={},
                .libraries={},.file_inputs={},.side_outputs={}},.options={},.cache_file={},.working_directory={}}};
    ArchivedCompileWave cache_evidence;
};
struct ArchivedStaticTargetClaim {
    ArchivedStaticTargetCompletion result;
    StaticTargetArtifactRecord record{.caller_label={},.compiler_options={},.sources={},.additional_object_inputs={},
        .archive={.completion=ArtifactCompletion::reused,.cache_state=ArtifactCacheState::reused,
            .association={.librarian={},.signature=BuildSignature::from_digest({}),.objects={},.output={}},
            .architecture=Architecture::x64,.link_time_code_generation=false,.additional_arguments={},
            .cache_file={},.working_directory={}}};
    ArchivedCompileWave cache_evidence;
};
struct ArchivedArtifactGenerationInput {
    std::string source_id;
    ArtifactTargetKey target;
    std::optional<std::string> generation;
    std::variant<ArchivedTargetClaim, ArchivedStaticTargetClaim> record;
    std::optional<LinkFactSnapshot> snapshot;
};
struct ArtifactGenerationArchive {
    std::filesystem::path lexical_root;
    std::vector<ArchivedArtifactGenerationInput> records;
    std::vector<ArtifactGenerationKey> retain;
    static constexpr bool producer_identity_verified = false;
    static constexpr bool current_content_verified = false;
    static constexpr bool complete_producer_inventory = false;
    static constexpr bool deletion_authorized = false;
};
struct ArtifactGenerationArchiveLimits {
    static constexpr std::size_t document_bytes = 16 * 1024 * 1024;
    static constexpr std::size_t string_bytes = 1024 * 1024;
    static constexpr std::size_t text_bytes = ArtifactGenerationLimits::text_bytes;
    static constexpr std::size_t items = ArtifactGenerationLimits::items;
    static constexpr std::size_t fields = 262144;
};
enum class ArtifactGenerationArchiveErrorCode {
    invalid_document, unsupported_version, limit_exceeded, invalid_evidence,
};
struct ArtifactGenerationArchiveError {
    ArtifactGenerationArchiveErrorCode code;
    std::string message;
    std::size_t offset{}; // Wire byte offset where available, not a file position.
    std::optional<RecordedArtifactGenerationError> model_error;
};

// Pure owning selection from completed ordinary EXE/DLL/static inputs. Whole
// selected wire shape and original model bounds precede copying. No IO or new
// compiler/linker/observer call; invalid evidence fails as a whole. Missing
// provenance and contradictory history remain the existing model's issues.
[[nodiscard]] std::expected<ArtifactGenerationArchive, ArtifactGenerationArchiveError>
project_artifact_generation_archive(std::span<const RecordedArtifactGenerationInput> records,
                                    std::span<const ArtifactGenerationKey> retain,
                                    const std::filesystem::path& lexical_root,
                                    const StoragePathKey& key);

// The SAME strict projection, recipe, lineage and lexical association as live
// recorded inputs; consumes claims directly, never recreates a RecordedResult.
[[nodiscard]] std::expected<RecordedArtifactGenerationModel, RecordedArtifactGenerationError>
model_archived_artifact_generations(const ArtifactGenerationArchive& archive,
                                    const StoragePathKey& key);

// Fixed v1 binary schema, explicit type and little-endian fixed-width lengths.
// Decode performs a complete non-owning structural/UTF-8/enum/budget pass before
// allocating document strings, paths or vectors. The existing bounded snapshot
// codec runs only after that pass. Both directions apply the strict shared
// semantic model; illegal wire data has no partial success. Keys are pure and
// lexical; callbacks and allocation exceptions propagate. Host path semantics
// must be compatible with the captured paths to compare models across machines.
// No checksum or successful round trip authenticates producer/current content,
// inventory completeness, or deletion rights. There is no default CLI caller.
[[nodiscard]] std::expected<std::string, ArtifactGenerationArchiveError>
encode_artifact_generation_archive(const ArtifactGenerationArchive& archive,
                                   const StoragePathKey& key);
[[nodiscard]] std::expected<ArtifactGenerationArchive, ArtifactGenerationArchiveError>
decode_artifact_generation_archive(std::string_view bytes, const StoragePathKey& key);

} // namespace mqb::orchestration
