#pragma once

#include <cstddef>
#include <functional>
#include "mqb/core/StorageInventory.hpp"

namespace mqb {
enum class StorageFileObservationState {
    not_attempted, observed, missing_leaf, unavailable, invalid_path,
    reparse_rejected, not_regular_file, limit_exceeded,
};

// A finite observation through one open leaf HANDLE, NOT a producer identity.
// Metadata is not a content hash, an atomic multi-file snapshot, or permission
// to retire/delete anything. No HANDLE is retained by this value.
struct StorageFileObservation {
    std::filesystem::path requested_path;
    StorageFileObservationState state{StorageFileObservationState::not_attempted};
    std::string physical_id;
    std::optional<std::uint64_t> logical_bytes;
    std::optional<std::uint64_t> allocated_bytes;
    std::optional<std::uint32_t> hard_links;
    std::vector<StorageIssue> issues;
    std::size_t opened_components{};

    static constexpr std::size_t maximum_depth = 128;
    static constexpr std::size_t maximum_path_characters = 32700;
    static constexpr bool producer_identity_verified = false;
    static constexpr bool current_content_verified = false;
    static constexpr bool complete_producer_inventory = false;
    static constexpr bool deletion_authorized = false;
};
using StoragePathObserver = std::function<StorageFileObservation(const std::filesystem::path&)>;
} // namespace mqb
