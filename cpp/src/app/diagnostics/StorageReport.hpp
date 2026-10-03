#pragma once
#include <iosfwd>
#include "mqb/core/StorageInventory.hpp"
namespace mqb::app::diagnostics {
// Uses locale-independent decimal formatting without changing the caller's
// formatting state or streambuf locale. Output failures reach the caller stream
// and respect its exception mask; invalid UTF-8 continues to throw.
// Returns false for an output-stream error, never silently truncates a report.
[[nodiscard]] bool write_storage_report(std::ostream& out, const StorageInventory& inventory, bool json);
}
