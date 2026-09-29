#include "mqb/orchestration/LinkCompletionArchive.hpp"
#include "LinkCompletionArchiveOperation.hpp"

namespace mqb::orchestration {
LinkCompletionArchiveResult archive_link_completion(
    const ObservedLinkResult& completed, const std::filesystem::path& destination,
    std::optional<std::string_view> capture_label) {
    return detail::archive_link_completion_with(completed, destination, capture_label,
        platform::windows::create_link_fact_snapshot_file);
}
} // namespace mqb::orchestration
