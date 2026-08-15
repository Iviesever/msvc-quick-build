#include "SourceSelectionGraph.hpp"

#include <algorithm>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "../indexing/DiscoveryPath.hpp"

namespace mqb::discovery::detail {
namespace {

namespace fs = std::filesystem;

void add_undirected_edge(
    std::vector<std::vector<std::size_t>>& adjacency,
    const std::size_t left,
    const std::size_t right) {
    if (left == right) {
        return;
    }
    auto& left_edges = adjacency[left];
    if (std::find(left_edges.begin(), left_edges.end(), right) == left_edges.end()) {
        left_edges.push_back(right);
    }
    auto& right_edges = adjacency[right];
    if (std::find(right_edges.begin(), right_edges.end(), left) == right_edges.end()) {
        right_edges.push_back(left);
    }
}

[[nodiscard]] bool ordinary_translation_unit(const IndexedFile& file) {
    return file.kind == IndexedFileKind::translation_unit
        && file.translation_unit_kind == TranslationUnitKind::source;
}

[[nodiscard]] bool module_interface_translation_unit(const IndexedFile& file) {
    return file.kind == IndexedFileKind::translation_unit
        && file.translation_unit_kind == TranslationUnitKind::module_interface;
}

[[nodiscard]] std::string ownership_key(const fs::path& path) {
    return path_key(path.parent_path() / path.stem());
}

void connect_include(
    std::vector<std::vector<std::size_t>>& adjacency,
    const std::vector<IndexedFile>& files,
    const std::unordered_map<std::string, std::size_t>& index_by_path,
    const std::vector<fs::path>& include_directories,
    const std::size_t file_index,
    const std::string& include,
    const bool quoted) {
    const auto& file = files[file_index];
    if (quoted) {
        const fs::path local = (file.path.parent_path() / fs::u8path(include)).lexically_normal();
        const auto found = index_by_path.find(path_key(local));
        if (found != index_by_path.end()) {
            add_undirected_edge(adjacency, file_index, found->second);
            return;
        }
    }

    for (const auto& include_directory : include_directories) {
        const fs::path candidate = (include_directory / fs::u8path(include)).lexically_normal();
        const auto found = index_by_path.find(path_key(candidate));
        if (found != index_by_path.end()) {
            add_undirected_edge(adjacency, file_index, found->second);
            return;
        }
    }
}

} // namespace

bool file_requires_module_pipeline(const IndexedFile& file) {
    return module_interface_translation_unit(file)
        || file.module_syntax.declared_module.has_value()
        || !file.module_syntax.imported_modules.empty()
        || file.module_syntax.imports_header_unit;
}

SourceSelection select_source_closure(
    const std::vector<IndexedFile>& files,
    const std::unordered_map<std::string, std::size_t>& index_by_path,
    const std::vector<fs::path>& include_directories,
    const std::unordered_set<std::string>& excluded_source_keys,
    const std::size_t entry_index) {
    std::vector<std::vector<std::size_t>> adjacency(files.size());

    for (std::size_t file_index = 0; file_index < files.size(); ++file_index) {
        const auto& file = files[file_index];
        for (const auto& include : file.quoted_includes) {
            connect_include(
                adjacency,
                files,
                index_by_path,
                include_directories,
                file_index,
                include,
                true);
        }
        for (const auto& include : file.angle_includes) {
            connect_include(
                adjacency,
                files,
                index_by_path,
                include_directories,
                file_index,
                include,
                false);
        }
    }

    std::unordered_map<std::string, std::vector<std::size_t>> ordinary_sources_by_owner;
    for (std::size_t index = 0; index < files.size(); ++index) {
        if (ordinary_translation_unit(files[index])) {
            ordinary_sources_by_owner[ownership_key(files[index].path)].push_back(index);
        }
    }
    for (std::size_t index = 0; index < files.size(); ++index) {
        if (files[index].kind != IndexedFileKind::header) {
            continue;
        }
        const auto owners = ordinary_sources_by_owner.find(ownership_key(files[index].path));
        if (owners == ordinary_sources_by_owner.end()) {
            continue;
        }
        for (const std::size_t owner : owners->second) {
            add_undirected_edge(adjacency, index, owner);
        }
    }

    std::unordered_map<std::string, std::vector<std::size_t>> interface_providers;
    std::unordered_map<std::string, std::vector<std::size_t>> declared_module_units;
    for (std::size_t index = 0; index < files.size(); ++index) {
        if (files[index].kind != IndexedFileKind::translation_unit
            || !files[index].module_syntax.declared_module) {
            continue;
        }
        const std::string& logical_name = *files[index].module_syntax.declared_module;
        declared_module_units[logical_name].push_back(index);
        if (module_interface_translation_unit(files[index])) {
            interface_providers[logical_name].push_back(index);
        }
    }

    for (std::size_t index = 0; index < files.size(); ++index) {
        if (files[index].kind != IndexedFileKind::translation_unit) {
            continue;
        }
        for (const auto& imported : files[index].module_syntax.imported_modules) {
            const auto providers = interface_providers.find(imported);
            if (providers == interface_providers.end()) {
                continue;
            }
            for (const std::size_t provider : providers->second) {
                add_undirected_edge(adjacency, index, provider);
            }
        }
    }

    for (const auto& [logical_name, units] : declared_module_units) {
        static_cast<void>(logical_name);
        if (units.size() < 2) {
            continue;
        }
        const std::size_t first = units.front();
        for (std::size_t offset = 1; offset < units.size(); ++offset) {
            add_undirected_edge(adjacency, first, units[offset]);
        }
    }

    std::vector<bool> visited(files.size(), false);
    std::deque<std::size_t> queue;
    queue.push_back(entry_index);
    visited[entry_index] = true;
    while (!queue.empty()) {
        const std::size_t current = queue.front();
        queue.pop_front();
        for (const std::size_t next : adjacency[current]) {
            if (visited[next]) {
                continue;
            }
            visited[next] = true;

            const bool second_main = next != entry_index
                && ordinary_translation_unit(files[next])
                && files[next].defines_main;
            const bool excluded_source = files[next].kind == IndexedFileKind::translation_unit
                && excluded_source_keys.contains(path_key(files[next].path));
            if (!second_main && !excluded_source) {
                queue.push_back(next);
            }
        }
    }

    SourceSelection result;
    for (std::size_t index = 0; index < files.size(); ++index) {
        if (!visited[index] || files[index].kind != IndexedFileKind::translation_unit) {
            continue;
        }
        if (index != entry_index
            && ordinary_translation_unit(files[index])
            && files[index].defines_main) {
            continue;
        }
        if (excluded_source_keys.contains(path_key(files[index].path))) {
            continue;
        }
        result.selected_indices.push_back(index);
    }
    return result;
}

} // namespace mqb::discovery::detail
