#pragma once

#include "mqb/orchestration/LinkCompletionArchive.hpp"
#include "mqb/orchestration/LinkFactSnapshotProjection.hpp"
#include <utility>

namespace mqb::orchestration::detail {
// Private IO seam for deterministic error forwarding tests. The production
// adapter binds only the existing create-only file implementation.
template<class Create>
LinkCompletionArchiveResult archive_link_completion_with(
    const ObservedLinkResult& completed, const std::filesystem::path& destination,
    std::optional<std::string_view> capture_label, Create&& create) {
    auto snapshot = capture_link_fact_snapshot(completed, capture_label);
    if (!snapshot) return std::unexpected(LinkCompletionArchiveError{std::move(snapshot.error())});
    auto stored = std::forward<Create>(create)(destination, *snapshot);
    if (!stored) return std::unexpected(LinkCompletionArchiveError{std::move(stored.error())});
    return *stored;
}
} // namespace mqb::orchestration::detail
