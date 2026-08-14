#include "mqb/discovery/SourceDiscovery.hpp"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include "mqb/core/TranslationUnitClassifier.hpp"
#include "indexing/DiscoveryPath.hpp"
#include "indexing/SourceIndex.hpp"
#include "selection/SourceSelectionGraph.hpp"

namespace mqb::discovery {
namespace {

namespace fs = std::filesystem;
using detail::path_key;

[[nodiscard]] Error failure(
    const ErrorCode code,
    fs::path path,
    std::string message) {
    return Error{
        .code = code,
        .path = std::move(path),
        .message = std::move(message),
    };
}

[[nodiscard]] std::expected<fs::path, Error> normalize_directory_correction(
    const fs::path& requested,
    const fs::path& root) {
    std::error_code error_code;
    fs::path absolute = fs::absolute(requested, error_code).lexically_normal();
    if (error_code
        || !fs::is_directory(absolute, error_code)
        || error_code
        || !detail::inside_root(root, absolute)) {
        return std::unexpected(failure(
            ErrorCode::invalid_correction,
            requested,
            "discovery excluded directory must be an existing directory strictly inside the project root"));
    }
    return absolute;
}

[[nodiscard]] std::expected<fs::path, Error> normalize_source_correction(
    const fs::path& requested,
    const fs::path& root,
    const std::string_view description) {
    std::error_code error_code;
    fs::path absolute = fs::absolute(requested, error_code).lexically_normal();
    if (error_code
        || !fs::is_regular_file(absolute, error_code)
        || error_code
        || !is_translation_unit_path(absolute)
        || !detail::inside_root(root, absolute)) {
        return std::unexpected(failure(
            ErrorCode::invalid_correction,
            requested,
            std::string{description}
                + " must be an existing supported C++ translation unit inside the project root"));
    }
    return absolute;
}

} // namespace

std::expected<Result, Error>
SourceDiscovery::discover(const Request& request) {
    std::error_code error_code;
    fs::path root = fs::absolute(request.project_root, error_code).lexically_normal();
    if (error_code || !fs::is_directory(root, error_code) || error_code) {
        return std::unexpected(failure(
            ErrorCode::invalid_project_root,
            request.project_root,
            "source discovery project root must be an existing directory"));
    }

    fs::path entry = fs::absolute(request.entry, error_code).lexically_normal();
    if (error_code
        || !fs::is_regular_file(entry, error_code)
        || error_code
        || !is_translation_unit_path(entry)
        || !detail::inside_root(root, entry)) {
        return std::unexpected(failure(
            ErrorCode::invalid_entry,
            request.entry,
            "source discovery entry must be a supported C++ translation unit inside the project root"));
    }

    std::vector<fs::path> excluded_directories;
    std::unordered_set<std::string> excluded_directory_keys;
    excluded_directories.reserve(request.excluded_directories.size());
    for (const auto& requested : request.excluded_directories) {
        auto directory = normalize_directory_correction(requested, root);
        if (!directory) {
            return std::unexpected(directory.error());
        }
        if (detail::same_or_inside(*directory, entry)) {
            return std::unexpected(failure(
                ErrorCode::invalid_correction,
                *directory,
                "discovery excluded directory must not contain the entry translation unit"));
        }
        if (excluded_directory_keys.insert(path_key(*directory)).second) {
            excluded_directories.push_back(std::move(*directory));
        }
    }

    std::unordered_set<std::string> excluded_source_keys;
    for (const auto& requested : request.excluded_sources) {
        auto source = normalize_source_correction(requested, root, "discovery excluded source");
        if (!source) {
            return std::unexpected(source.error());
        }
        if (*source == entry) {
            return std::unexpected(failure(
                ErrorCode::invalid_correction,
                *source,
                "discovery excluded source must not be the entry translation unit"));
        }
        excluded_source_keys.insert(path_key(*source));
    }

    std::vector<fs::path> extra_sources;
    std::unordered_set<std::string> extra_source_keys;
    for (const auto& requested : request.extra_sources) {
        auto source = normalize_source_correction(requested, root, "discovery extra source");
        if (!source) {
            return std::unexpected(source.error());
        }
        const std::string key = path_key(*source);
        if (excluded_source_keys.contains(key)) {
            return std::unexpected(failure(
                ErrorCode::invalid_correction,
                *source,
                "the same source cannot be both extra and excluded"));
        }
        for (const auto& directory : excluded_directories) {
            if (detail::same_or_inside(directory, *source)) {
                return std::unexpected(failure(
                    ErrorCode::invalid_correction,
                    *source,
                    "discovery extra source must not be inside an excluded directory"));
            }
        }
        if (*source != entry && extra_source_keys.insert(key).second) {
            auto analysis = detail::read_source_analysis(*source, false);
            if (!analysis) {
                return std::unexpected(failure(
                    ErrorCode::invalid_correction,
                    *source,
                    analysis.error()));
            }
            if (classify_translation_unit_path(*source) == TranslationUnitKind::source
                && analysis->defines_main) {
                return std::unexpected(failure(
                    ErrorCode::invalid_correction,
                    *source,
                    "discovery extra source must not define another main()"));
            }
            extra_sources.push_back(std::move(*source));
        }
    }

    auto indexed = detail::build_source_index(root, excluded_directory_keys);
    if (!indexed) {
        return std::unexpected(indexed.error());
    }

    Result result;
    result.indexed_files = indexed->files.size();
    result.warnings = std::move(indexed->warnings);

    const auto entry_it = indexed->index_by_path.find(path_key(entry));
    if (entry_it == indexed->index_by_path.end()) {
        return std::unexpected(failure(
            ErrorCode::invalid_entry,
            entry,
            "source discovery entry was not indexed"));
    }

    std::vector<fs::path> include_directories;
    include_directories.reserve(request.include_directories.size());
    for (const auto& directory : request.include_directories) {
        fs::path absolute = fs::absolute(directory, error_code).lexically_normal();
        if (!error_code) {
            include_directories.push_back(std::move(absolute));
        }
        error_code.clear();
    }

    const auto selection = detail::select_source_closure(
        indexed->files,
        indexed->index_by_path,
        include_directories,
        excluded_source_keys,
        entry_it->second);
    result.sources.reserve(selection.selected_indices.size() + extra_sources.size());
    for (const std::size_t index : selection.selected_indices) {
        result.sources.push_back(indexed->files[index].path);
    }

    for (const auto& source : extra_sources) {
        const std::string key = path_key(source);
        const auto duplicate = std::find_if(
            result.sources.begin(),
            result.sources.end(),
            [&](const fs::path& existing) { return path_key(existing) == key; });
        if (duplicate == result.sources.end()) {
            result.sources.push_back(source);
        }
    }

    std::sort(
        result.sources.begin(),
        result.sources.end(),
        [](const fs::path& left, const fs::path& right) {
            return path_key(left) < path_key(right);
        });
    const auto entry_position = std::find(result.sources.begin(), result.sources.end(), entry);
    if (entry_position != result.sources.end() && entry_position != result.sources.begin()) {
        std::rotate(result.sources.begin(), entry_position, entry_position + 1);
    }

    for (const auto& source : result.sources) {
        const auto selected = indexed->index_by_path.find(path_key(source));
        if (selected != indexed->index_by_path.end()
            && detail::file_requires_module_pipeline(indexed->files[selected->second])) {
            result.requires_module_pipeline = true;
            break;
        }
    }

    if (result.sources.empty() || result.sources.front() != entry) {
        return std::unexpected(failure(
            ErrorCode::invalid_entry,
            entry,
            "source discovery did not retain the entry translation unit"));
    }
    return result;
}

} // namespace mqb::discovery
