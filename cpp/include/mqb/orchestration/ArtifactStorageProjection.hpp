#pragma once

#include "mqb/core/ArtifactStorageAssociation.hpp"
#include "mqb/orchestration/MsvcIncrementalCompileCoordinator.hpp"

namespace mqb::orchestration {
struct ModuleScanArtifactRecord;
struct ModuleCompileWaveArtifactRecord;
struct ModuleTargetArtifactRecord;

// Pure, owned projections of selected typed associations. No process/env dump,
// command-line/path guessing, provider parser, filesystem access or execution.
// Configuration is an annotation, not complete recipe identity; metadata paths
// may have save_failed or no saved outcome. Original records remain separate.
[[nodiscard]] ArtifactStorageReferences project_storage_references(const LinkArtifactRecord& record);
[[nodiscard]] ArtifactStorageReferences project_storage_references(const ArchiveArtifactRecord& record);
[[nodiscard]] ArtifactStorageReferences project_storage_references(const TargetArtifactRecord& record);
[[nodiscard]] ArtifactStorageReferences project_storage_references(const StaticTargetArtifactRecord& record);
[[nodiscard]] ArtifactStorageReferences project_storage_references(const PchArtifactRecord& record);
[[nodiscard]] ArtifactStorageReferences project_storage_references(const ModuleCompileArtifactRecord& record);
[[nodiscard]] ArtifactStorageReferences project_storage_references(const ModuleScanArtifactRecord& record);
[[nodiscard]] ArtifactStorageReferences project_storage_references(const ModuleCompileWaveArtifactRecord& record);
[[nodiscard]] ArtifactStorageReferences project_storage_references(const ModuleTargetArtifactRecord& record);

// BEGIN recorded storage projection interface
// Explicit ordinary-target consumer. This is not the record-only projection:
// reject inconsistent supplied values instead of inventing missing evidence.
struct RecordedTargetResult;
struct RecordedStaticTargetResult;
struct RecordedStorageProjectionLimits {
    std::size_t sources{100000};
    std::size_t items{1000000};
    std::size_t text_code_units{64 * 1024 * 1024};
};
enum class RecordedStorageProjectionIssue {
    limit_exceeded, source_count, source_mismatch, options_mismatch,
    cache_mismatch, outcome_mismatch, invalid_path, path_conflict, terminal_mismatch,
};
struct RecordedStorageProjectionError {
    RecordedStorageProjectionIssue issue;
    std::optional<std::size_t> source_index;
    std::string message;
};
struct CompileStorageContext {
    // Both identities are historical claims, not an authenticated producer.
    // Do not copy MsvcToolchain::environment or normalize differing identities.
    ToolchainIdentity inspection_toolchain;
    ToolchainIdentity cache_toolchain;
    BuildSignature captured_signature;
    bool toolchains_differ{false};
    bool force_rebuild{false};
    std::optional<CompileCacheFileError> save_error;
    std::vector<IncrementalCompileWarning> warnings;
};
struct RecordedTargetStorageReferences {
    ArtifactStorageReferences references;
    // Entry i describes compile stage i; the terminal stage follows these.
    std::vector<CompileStorageContext> compiles;
    static constexpr bool producer_identity_verified = false;
    static constexpr bool current_content_verified = false;
    static constexpr bool complete_producer_inventory = false;
    static constexpr bool deletion_authorized = false;
};
// Pure selected-field projection, no signature rebuilding or cache validation
// against today's filesystem. Source/order/request/cache/terminal disagreement
// fails as a whole. Only the already captured final wave is consumed. The path
// key must be the platform's pure lexical authority; its exceptions propagate.
// Critical paths require an absolute spelling or their own recorded absolute
// cwd, without NUL/parent traversal. Other input references remain literal.
// Limits may be lowered, never raised beyond the defaults. They bound selected
// input traversal and copied output, not the caller's existing allocation.
// Old overloads keep unknown cwd/cache outcomes and all false authority flags.
[[nodiscard]] std::expected<RecordedTargetStorageReferences, RecordedStorageProjectionError>
project_storage_references(const RecordedTargetResult&, const StoragePathKey&,
                           RecordedStorageProjectionLimits = {});
[[nodiscard]] std::expected<RecordedTargetStorageReferences, RecordedStorageProjectionError>
project_storage_references(const RecordedStaticTargetResult&, const StoragePathKey&,
                           RecordedStorageProjectionLimits = {});
// END recorded storage projection interface
} // namespace mqb::orchestration
