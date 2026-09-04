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

#include "mqb/core/PerformanceEvidence.hpp"
#include "mqb/core/TranslationUnitClassifier.hpp"
#include "cache/DiscoveryCache.hpp"
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

[[nodiscard]] std::vector<fs::path> normalize_include_directories(
    const std::vector<fs::path>& requested) {
    std::vector<fs::path> result;
    result.reserve(requested.size());
    std::error_code error_code;
    for (const auto& directory : requested) {
        fs::path absolute = fs::absolute(directory, error_code).lexically_normal();
        if (!error_code) {
            result.push_back(std::move(absolute));
        }
        error_code.clear();
    }
    return result;
}

[[nodiscard]] std::optional<fs::path> prepare_cache_file(
    const bool enabled,
    const std::optional<fs::path>& requested,
    const fs::path& root) {
    if (!enabled) return std::nullopt;
    try {
        const fs::path candidate = requested && !requested->empty()
            ? *requested
            : root / ".mqb" / "cache" / "discovery" / "source-discovery.mqbcache";
        std::error_code error_code;
        fs::path absolute = fs::absolute(candidate, error_code).lexically_normal();
        if (error_code) return std::nullopt;
        return absolute;
    } catch (...) {
        return std::nullopt;
    }
}

void stabilize_cache_parent_best_effort(const fs::path& cache_file) noexcept {
    try {
        if (cache_file.parent_path().empty()) return;
        std::error_code error_code;
        fs::create_directories(cache_file.parent_path(), error_code);
    } catch (...) {
        return;
    }
}

} // namespace

std::expected<Result, Error>
SourceDiscovery::discover(const Request& request) {
    mqb::diagnostics::finish_project_setup();

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

    std::vector<fs::path> excluded_sources;
    std::unordered_set<std::string> excluded_source_keys;
    excluded_sources.reserve(request.excluded_sources.size());
    for (const auto& requested : request.excluded_sources) {
        auto source = normalize_source_correction(requested, root, "discovery excluded source");
        if (!source) {
            return std::unexpected(source.error());
        }
        if (path_key(*source) == path_key(entry)) {
            return std::unexpected(failure(
                ErrorCode::invalid_correction,
                *source,
                "discovery excluded source must not be the entry translation unit"));
        }
        if (excluded_source_keys.insert(path_key(*source)).second) {
            excluded_sources.push_back(std::move(*source));
        }
    }

    std::vector<fs::path> extra_sources;
    std::unordered_set<std::string> extra_source_keys;
    extra_sources.reserve(request.extra_sources.size());
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
        if (key != path_key(entry) && extra_source_keys.insert(key).second) {
            extra_sources.push_back(std::move(*source));
        }
    }

    std::vector<fs::path> include_directories =
        normalize_include_directories(request.include_directories);
    const detail::DiscoveryRequestIdentity cache_identity{
        .project_root = root,
        .entry = entry,
        .include_directories = include_directories,
        .forced_includes = request.forced_includes,
        .excluded_directories = excluded_directories,
        .extra_sources = extra_sources,
        .excluded_sources = excluded_sources,
    };
    const std::optional<fs::path> cache_file = prepare_cache_file(
        request.persistent_cache,
        request.cache_file,
        root);
    if (cache_file) {
        if (auto cached = detail::try_reuse_discovery_cache(*cache_file, cache_identity)) {
            return std::move(*cached);
        }
        // Stabilize creation of the .mqb/cache hierarchy before directory
        // freshness evidence is captured by a full discovery pass.
        stabilize_cache_parent_best_effort(*cache_file);
    }

    for (const auto& source : extra_sources) {
        auto analysis = detail::read_source_analysis(source, false);
        if (!analysis) {
            return std::unexpected(failure(
                ErrorCode::invalid_correction,
                source,
                analysis.error()));
        }
        if (classify_translation_unit_path(source) == TranslationUnitKind::source
            && analysis->defines_main) {
            return std::unexpected(failure(
                ErrorCode::invalid_correction,
                source,
                "discovery extra source must not define another main()"));
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

    const auto selection = detail::select_source_closure(
        indexed->files,
        indexed->index_by_path,
        include_directories,
        request.forced_includes,
        excluded_source_keys,
        entry_it->second);
    if (!selection.unresolved_forced_includes.empty()) {
        return std::unexpected(failure(
            ErrorCode::unresolved_forced_include,
            selection.unresolved_forced_includes.front(),
            "compiler /FI forced include must resolve to an indexed project header under smart discovery; use --no-discover only when source selection is explicitly managed"));
    }

    result.sources.reserve(selection.selected_indices.size() + extra_sources.size());
    for (const std::size_t index : selection.selected_indices) {
        result.sources.push_back(indexed->files[index].path);
    }

    bool extras_are_indexed = true;
    for (const auto& source : extra_sources) {
        const std::string key = path_key(source);
        if (!indexed->index_by_path.contains(key)) {
            extras_are_indexed = false;
        }
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
    const std::string entry_key = path_key(entry);
    const auto entry_position = std::find_if(
        result.sources.begin(),
        result.sources.end(),
        [&](const fs::path& source) { return path_key(source) == entry_key; });
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

    if (result.sources.empty() || path_key(result.sources.front()) != entry_key) {
        return std::unexpected(failure(
            ErrorCode::invalid_entry,
            entry,
            "source discovery did not retain the entry translation unit"));
    }

    if (cache_file
        && indexed->cacheable
        && extras_are_indexed
        && result.warnings.empty()) {
        detail::save_discovery_cache_best_effort(
            *cache_file,
            detail::DiscoveryCacheRecord{
                .request = cache_identity,
                .result = result,
                .files = indexed->file_snapshots,
                .directories = indexed->directory_snapshots,
            });
    }
    return result;
}

} // namespace mqb::discovery
