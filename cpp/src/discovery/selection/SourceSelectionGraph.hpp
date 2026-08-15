#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../indexing/SourceIndex.hpp"

namespace mqb::discovery::detail {

struct SourceSelection {
    std::vector<std::size_t> selected_indices;
    std::vector<std::filesystem::path> unresolved_forced_includes;
};

[[nodiscard]] SourceSelection select_source_closure(
    const std::vector<IndexedFile>& files,
    const std::unordered_map<std::string, std::size_t>& index_by_path,
    const std::vector<std::filesystem::path>& include_directories,
    const std::vector<std::filesystem::path>& forced_includes,
    const std::unordered_set<std::string>& excluded_source_keys,
    std::size_t entry_index);

[[nodiscard]] bool file_requires_module_pipeline(const IndexedFile& file);

} // namespace mqb::discovery::detail
