#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "mqb/core/WriteInventory.hpp"
#include "mqb/platform/windows/WindowsWriteDomain.hpp"

namespace mqb::platform::windows {
struct PhysicalWriteObservation {
    KnownWrite declaration;
    std::optional<std::size_t> directory_index;
    // Finite read-only leaf observation. Missing file is different from missing
    // parent, inaccessible file, reparse point or multiple hard-link ambiguity.
    std::optional<WriteDirectoryIdentity> existing_file;
    bool file_absent{false};
    std::optional<WriteDomainError> error;
};

// Read-only mapping of KNOWN writes, retaining all unenumerated-effect reasons.
// Private directory pins expose no reservation/withdrawal API. There is no
// ready/complete/lease-safe predicate: even zero mapping errors does not close
// a build's write set or cover future pathname replacement races.
class WindowsWriteInventory {
public:
    [[nodiscard]] static WindowsWriteInventory inspect(const WriteInventory&);
    WindowsWriteInventory(WindowsWriteInventory&&) noexcept = default;
    WindowsWriteInventory& operator=(WindowsWriteInventory&&) noexcept = default;
    WindowsWriteInventory(const WindowsWriteInventory&) = delete;
    WindowsWriteInventory& operator=(const WindowsWriteInventory&) = delete;

    [[nodiscard]] std::span<const PhysicalWriteObservation> entries() const noexcept { return entries_; }
    [[nodiscard]] std::span<const UnresolvedWrite> unresolved() const noexcept { return unresolved_; }
    [[nodiscard]] std::size_t directory_count() const noexcept { return pins_.size(); }
    [[nodiscard]] std::optional<WriteDirectoryIdentity> directory_identity(std::size_t index) const noexcept {
        return index < pins_.size() ? pins_[index].identity() : std::nullopt;
    }
private:
    WindowsWriteInventory() = default;
    std::vector<WindowsWriteDomain> pins_;
    std::vector<PhysicalWriteObservation> entries_;
    std::vector<UnresolvedWrite> unresolved_;
};
} // namespace mqb::platform::windows
