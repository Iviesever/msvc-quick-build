#pragma once

#include "mqb/core/ArtifactStorageAssociation.hpp"

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
} // namespace mqb::orchestration
