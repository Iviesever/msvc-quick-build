#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mqb/core/ArchiveCache.hpp"
#include "mqb/core/CompilerOptions.hpp"
#include "mqb/core/LinkCache.hpp"
#include "mqb/core/LinkOptions.hpp"

namespace mqb {

// Invocation outcomes, not file ownership or current-content certificates.
// A cache hit is a reuse observation; it never invents a new produced generation.
enum class ArtifactCompletion { executed, reused };
enum class ArtifactCacheState { saved, reused, save_failed };

// Optional caller annotations. Never parse a filename/hash into these labels,
// assume uniqueness, or use them to authorize retention/deletion.
struct ArtifactGenerationLabel {
    std::string target;
    std::string generation;
};

struct LinkArtifactRecord {
    ArtifactCompletion completion;
    ArtifactCacheState cache_state;
    // The actual entry sealed by this successful LINK pass, or the exact entry
    // accepted by this invocation's cache validator. Not a second recipe model.
    LinkCacheEntry association;
    LinkOptions options;
    std::filesystem::path cache_file;
    std::optional<std::filesystem::path> working_directory;

    static constexpr bool deletion_authorized = false;
    static constexpr bool physical_identity_verified = false;
};

// Effective LIB recipe fields come from the existing parameter authority.
// The recipe signature does NOT encode compiler Debug/Release configuration.
struct ArchiveArtifactRecord {
    ArtifactCompletion completion;
    ArtifactCacheState cache_state;
    ArchiveCacheEntry association;
    Architecture architecture;
    bool link_time_code_generation;
    std::vector<std::string> additional_arguments;
    std::filesystem::path cache_file;
    std::filesystem::path working_directory;

    static constexpr bool deletion_authorized = false;
    static constexpr bool physical_identity_verified = false;
};

struct SourceArtifactAssociation {
    std::filesystem::path source;
    std::filesystem::path object;
    // References only: warning-bearing builds may not have saved these files.
    std::filesystem::path dependencies;
    std::filesystem::path compile_cache;
    ArtifactCompletion completion;
    bool has_warnings{false};
};

struct TargetArtifactRecord {
    std::optional<ArtifactGenerationLabel> caller_label;
    CompilerOptions compiler_options;
    std::vector<SourceArtifactAssociation> sources;
    // Referenced inputs, NOT newly owned outputs. PCH/module producers must
    // eventually contribute their own records rather than being guessed here.
    std::vector<std::filesystem::path> additional_object_inputs;
    LinkArtifactRecord link;

    static constexpr bool deletion_authorized = false;
    static constexpr bool complete_producer_inventory = false;
};

struct StaticTargetArtifactRecord {
    std::optional<ArtifactGenerationLabel> caller_label;
    CompilerOptions compiler_options;
    std::vector<SourceArtifactAssociation> sources;
    // Additional objects were produced upstream, not by this static target.
    std::vector<std::filesystem::path> additional_object_inputs;
    ArchiveArtifactRecord archive;

    static constexpr bool deletion_authorized = false;
    static constexpr bool complete_producer_inventory = false;
};

} // namespace mqb
