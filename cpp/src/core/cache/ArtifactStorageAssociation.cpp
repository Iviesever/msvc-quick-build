#include "mqb/core/ArtifactStorageAssociation.hpp"

#include <map>
#include <utility>

namespace mqb {
namespace {
namespace fs = std::filesystem;
bool lexical_path(const fs::path& path) {
    if (path.native().find(fs::path::value_type{}) != fs::path::string_type::npos) return false;
    for (const auto& part : path) if (part == "..") return false;
    return true;
}
std::optional<fs::path> resolve(const fs::path& path, const std::optional<fs::path>& cwd) {
    if (path.empty() || !lexical_path(path)) return std::nullopt;
    if (path.is_absolute()) return path.lexically_normal();
    // In particular, do not reinterpret drive-relative/root-relative Windows
    // paths using the process cwd, another stage, or the inventory root.
    if (path.has_root_path() || !cwd || !cwd->is_absolute() || !lexical_path(*cwd)) return std::nullopt;
    return (*cwd / path).lexically_normal();
}
template<class T> bool differs(const std::optional<T>& a, const std::optional<T>& b) {
    return a && b && *a != *b;
}
} // namespace

std::expected<ArtifactStorageAssociation, ArtifactStorageAssociationError>
associate_artifact_storage(std::span<const ArtifactStorageReferences> records,
                           const StorageInventory& observation, const StoragePathKey& key) {
    auto invalid = [](const char* message) {
        return std::unexpected(ArtifactStorageAssociationError{message});
    };
    if (!key || !observation.artifact_root.is_absolute() || !lexical_path(observation.artifact_root))
        return invalid("an absolute lexical inventory root and a path-key function are required");
    constexpr std::size_t limit = 1000000;
    if (observation.entries.size() > limit) return invalid("inventory row limit exceeded");
    std::size_t path_count = 0;
    for (const auto& r : records) for (const auto& s : r.stages) {
        if (s.paths.size() > limit - path_count) return invalid("reference path limit exceeded");
        path_count += s.paths.size();
    }
    // Bound the otherwise multiplicative expansion of duplicate keys/hardlinks.
    // Refuse the whole result before exceeding the budget; never truncate it.
    std::size_t remaining_links = limit;
    ArtifactStorageAssociation result;
    result.records.assign(records.begin(), records.end());
    result.observation = observation;
    result.row_matches.resize(observation.entries.size());
    std::map<std::string, std::vector<std::size_t>> by_path, by_id;
    for (std::size_t i = 0; i < observation.entries.size(); ++i) {
        const auto& entry = observation.entries[i];
        if (entry.relative_path.has_root_path() || !lexical_path(entry.relative_path))
            return invalid("inventory row must be relative without NUL or parent traversal");
        const auto k = key((observation.artifact_root / entry.relative_path).lexically_normal());
        if (k.empty()) return invalid("empty inventory path key");
        by_path[k].push_back(i);
        if (entry.kind == StorageEntryKind::file && !entry.physical_id.empty())
            by_id[entry.physical_id].push_back(i);
    }
    for (std::size_t r = 0; r < result.records.size(); ++r) {
        const auto& record = result.records[r];
        for (std::size_t s = 0; s < record.stages.size(); ++s) {
            const auto& stage = record.stages[s];
            for (std::size_t p = 0; p < stage.paths.size(); ++p) {
                ArtifactStorageMatch match;
                match.record_index = r; match.stage_index = s; match.path_index = p;
                match.resolved_path = resolve(stage.paths[p].path, stage.working_directory);
                if (match.resolved_path) {
                    const auto k = key(*match.resolved_path);
                    if (k.empty()) return invalid("empty record path key");
                    match.state = ArtifactPathObservation::not_observed;
                    if (const auto found = by_path.find(k); found != by_path.end()) {
                        if (found->second.size() > remaining_links) return invalid("association link limit exceeded");
                        remaining_links -= found->second.size();
                        match.observed_rows = found->second;
                        match.state = found->second.size() == 1 ? ArtifactPathObservation::observed_path
                                                              : ArtifactPathObservation::ambiguous_path;
                        for (const auto row : found->second) result.row_matches[row].push_back(result.matches.size());
                        if (found->second.size() == 1) {
                            const auto row = found->second[0];
                            const auto& entry = observation.entries[row];
                            if (entry.kind == StorageEntryKind::file && !entry.physical_id.empty()) {
                                const auto& aliases = by_id.at(entry.physical_id);
                                match.observed_identity_metadata_conflict = entry.hard_links && aliases.size() > *entry.hard_links;
                                for (const auto alias : aliases) {
                                    if (alias == row) continue;
                                    if (remaining_links == 0) return invalid("association alias limit exceeded");
                                    --remaining_links;
                                    match.same_observed_file_rows.push_back(alias);
                                    const auto& other = observation.entries[alias];
                                    match.observed_identity_metadata_conflict |=
                                        differs(entry.logical_bytes, other.logical_bytes) ||
                                        differs(entry.allocated_bytes, other.allocated_bytes) ||
                                        differs(entry.hard_links, other.hard_links) ||
                                        (other.hard_links && aliases.size() > *other.hard_links);
                                }
                            }
                        }
                    }
                }
                result.matches.push_back(std::move(match));
            }
        }
    }
    return result;
}
} // namespace mqb
