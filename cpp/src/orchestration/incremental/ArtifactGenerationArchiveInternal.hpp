#pragma once

#include "mqb/orchestration/ArtifactGenerationArchive.hpp"

namespace mqb::orchestration::detail {
// Count-only traversal of exactly the selected v1 wire fields, including new
// terminal warning claims. Shared with the codec, before owning archive copies.
[[nodiscard]] std::expected<void, ArtifactGenerationArchiveError>
preflight_recorded_generation_archive(std::span<const RecordedArtifactGenerationInput> records,
                                      std::span<const ArtifactGenerationKey> retain,
                                      const std::filesystem::path& lexical_root);
} // namespace mqb::orchestration::detail
