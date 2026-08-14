#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <vector>

#include "mqb/core/Artifact.hpp"
#include "mqb/core/BuildSignature.hpp"
#include "mqb/core/BuildTypes.hpp"
#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/FileSnapshot.hpp"
#include "mqb/core/ToolchainIdentity.hpp"
#include "mqb/core/TranslationUnit.hpp"

namespace mqb {

struct ModuleScanEvidence {
    BuildSignature signature;
    FileSnapshot source;
    FileSnapshot output;
    std::vector<FileSnapshot> dependencies;
};

struct CompileCacheEntry {
    std::filesystem::path source;
    TranslationUnitKind kind{TranslationUnitKind::source};
    ToolchainIdentity toolchain;
    BuildSignature signature;
    std::vector<Artifact> outputs;
    std::vector<std::filesystem::path> dependencies;
    std::optional<ModuleScanEvidence> module_scan;
};

struct CompileCacheValidation {
    std::vector<BuildReason> reasons;

    [[nodiscard]] bool reusable() const noexcept {
        return reasons.empty();
    }
};

class CompileCacheValidator {
public:
    [[nodiscard]] static CompileCacheValidation validate(
        const TranslationUnit& current_unit,
        const ToolchainIdentity& current_toolchain,
        const CompilerOptions& current_options,
        const std::optional<CompileCacheEntry>& cached_entry,
        const FileSnapshot& source_snapshot,
        std::span<const FileSnapshot> output_snapshots,
        std::span<const FileSnapshot> dependency_snapshots);
};

class ModuleScanEvidenceValidator {
public:
    [[nodiscard]] static bool reusable(
        const ModuleScanEvidence& evidence,
        const BuildSignature& current_signature,
        const FileSnapshot& current_source,
        const FileSnapshot& current_output,
        std::span<const FileSnapshot> current_dependencies);
};

} // namespace mqb
