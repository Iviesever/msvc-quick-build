#pragma once

#include "mqb/core/StorageInventory.hpp"

namespace mqb::platform::windows {

// Metadata-only walk plus synchronous, pinned cache observation. Never creates
// the root, follows a reparse point, runs tools, or writes any project state.
// This is a bounded best-effort observation, not an atomic snapshot or lease.
[[nodiscard]] StorageInventory scan_storage(
    const std::filesystem::path& artifact_root,
    const StorageFileObserver& observer = {});

} // namespace mqb::platform::windows
