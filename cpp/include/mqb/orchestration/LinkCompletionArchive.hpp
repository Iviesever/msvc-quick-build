#pragma once

#include "mqb/orchestration/ObservedLinkCompletion.hpp"
#include "mqb/platform/windows/LinkFactSnapshotFile.hpp"
#include <variant>

namespace mqb::orchestration {
// Preserve the original capture or file error, including partial-write/close data.
using LinkCompletionArchiveError = std::variant<LinkFactSnapshotError,
    platform::windows::LinkFactFileError>;
using LinkCompletionArchiveResult = std::expected<platform::windows::LinkFactFileReceipt,
    LinkCompletionArchiveError>;

// Explicit opt-in AFTER completion. Borrows, never moves/modifies the build result,
// warnings or observation. Capture once, create once; no build/observer callbacks,
// default path, retry, overwrite, cleanup or automatic readback. A failed build has
// no ObservedLinkResult to archive. bad_alloc and unexpected exceptions propagate;
// the caller still owns the original result. Success is not current-file authority.
[[nodiscard]] LinkCompletionArchiveResult archive_link_completion(
    const ObservedLinkResult& completed, const std::filesystem::path& destination,
    std::optional<std::string_view> capture_label = std::nullopt);
} // namespace mqb::orchestration
