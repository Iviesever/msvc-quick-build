#pragma once

#include "mqb/core/StorageFileObservation.hpp"

namespace mqb::platform::windows {

// One existing local-NTFS file only; no enumeration, content/cache read,
// creation, deletion or retained HANDLE. Limits bound work, not kernel IO time.
// Read pins reject reparse ancestors/leaf; identity is NOT producer authority.
[[nodiscard]] StorageFileObservation observe_storage_file(const std::filesystem::path& path);

} // namespace mqb::platform::windows
