#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace mqb {

enum class StorageEntryKind { file, directory, reparse_point, unavailable };

struct StorageIssue {
    std::filesystem::path path;
    std::string message;
    std::uint32_t native_code{};
};

struct StorageEntry {
    std::filesystem::path relative_path;
    StorageEntryKind kind{StorageEntryKind::unavailable};
    std::optional<std::uint64_t> logical_bytes;
    std::optional<std::uint64_t> allocated_bytes;
    std::optional<std::uint32_t> hard_links;
    std::string physical_id;
    bool protected_path{true};
    std::vector<std::size_t> references;
};

// These are historical cache references, NOT ownership or liveness records.
// Configuration cannot be recovered from the opaque signature. Never derive
// permission to remove a file from these paths, timestamps, or target names.
struct StorageReference {
    std::filesystem::path cache;
    std::filesystem::path output;
    std::string kind;
    std::string signature;
    std::string tool_version;
    std::vector<std::filesystem::path> paths;
};

struct StorageInventory {
    std::filesystem::path artifact_root;
    bool root_exists{false};
    std::vector<StorageEntry> entries;
    std::vector<StorageReference> references;
    std::vector<StorageIssue> issues;
};

using StoragePathKey = std::function<std::string(const std::filesystem::path&)>;
using StorageFileObserver = std::function<void(
    const std::filesystem::path&, const StorageEntry&, StorageInventory&)>;

// The caller must keep this file and all its path ancestors pinned against
// replacement/writing during the synchronous observation. Only legacy link and
// archive cache locations are read. Referenced paths are never opened.
void observe_storage_cache(
    const std::filesystem::path& file,
    const StorageEntry& entry,
    StorageInventory& inventory);

// Pure path association over the observed rows. The platform supplies its
// existing path-identity authority; no cache path is promoted to owned state.
void associate_storage_references(StorageInventory& inventory, const StoragePathKey& key);

} // namespace mqb
