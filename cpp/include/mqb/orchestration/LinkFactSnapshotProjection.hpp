#pragma once

#include "mqb/core/LinkFactSnapshot.hpp"
#include "mqb/orchestration/ObservedLinkCompletion.hpp"

namespace mqb::orchestration {
// Capture already obtained success/reuse, warnings and observation values.
// No request/coordinator/callback parameter or repeated build/inspect/observation.
// Does not call current_path(), read a clock, normalize a path, or load a cache.
// Failed builds have no ObservedLinkResult and cannot manufacture a success here.
[[nodiscard]] std::expected<LinkFactSnapshot, LinkFactSnapshotError>
capture_link_fact_snapshot(const ObservedLinkResult& completed,
                           std::optional<std::string_view> capture_label = std::nullopt);
} // namespace mqb::orchestration
