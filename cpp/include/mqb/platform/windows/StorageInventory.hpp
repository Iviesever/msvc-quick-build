#pragma once

#include "mqb/core/StorageInventory.hpp"
#include "mqb/core/StorageFileObservation.hpp"

namespace mqb::platform::windows {

// Metadata-only walk plus synchronous, pinned cache observation. Never creates
// the root, follows a reparse point, runs tools, or writes any project state.
// This is a bounded best-effort observation, not an atomic snapshot or lease.
[[nodiscard]] StorageInventory scan_storage(
    const std::filesystem::path& artifact_root,
    const StorageFileObserver& observer = {});

// Opt-in, one existing local-NTFS file only. Reuses the inventory's read pins;
// ancestors/final reparse points are refused. At most maximum_depth components,
// no directory enumeration, content/cache read, creation, or retained HANDLE.
// "Bounded" limits operations/memory, NOT a deadline on synchronous kernel IO.
[[nodiscard]] StorageFileObservation observe_storage_file(const std::filesystem::path& path);

} // namespace mqb::platform::windows
