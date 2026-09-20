#pragma once
#include <iosfwd>
#include "mqb/core/StorageInventory.hpp"
namespace mqb::app::diagnostics {
// Returns false for an output-stream error, never silently truncates a report.
[[nodiscard]] bool write_storage_report(std::ostream& out, const StorageInventory& inventory, bool json);
}
