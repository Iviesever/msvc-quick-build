#include "SourceIndex.hpp"

#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include "mqb/core/TranslationUnitClassifier.hpp"
#include "DiscoveryPath.hpp"

namespace mqb::discovery::detail {
namespace {

namespace fs = std::filesystem;

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

[[nodiscard]] std::expected<std::string, std::string>
read_text(const fs::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return std::unexpected("failed to open source-discovery input");
    }
    std::string text{
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{}};
    if (!stream.eof() && stream.fail()) {
        return std::unexpected("failed while reading source-discovery input");
    }
    return text;
}

[[nodiscard]] std::optional<FileSnapshot> snapshot_path(
    const fs::path& path,
    const bool directory) {
    std::error_code error_code;
    const auto status = fs::status(path, error_code);
    if (error_code) return std::nullopt;
    if (directory ? !fs::is_directory(status) : !fs::is_regular_file(status)) {
        return std::nullopt;
    }
    const auto modified = fs::last_write_time(path, error_code);
    if (error_code) return std::nullopt;
    return FileSnapshot{
        .path = path.lexically_normal(),
        .exists = true,
        .modified = modified,
    };
}

void seal_snapshots(
    std::vector<FileSnapshot>& snapshots,
    const bool directory,
    bool& cacheable) {
    if (!cacheable) return;
    for (auto& snapshot : snapshots) {
        auto current = snapshot_path(snapshot.path, directory);
        if (!current || current->modified != snapshot.modified) {
            cacheable = false;
            return;
        }
        snapshot = std::move(*current);
    }
}

} // namespace

std::expected<SourceTextAnalysis, std::string>
read_source_analysis(
    const fs::path& path,
    const bool parse_module_syntax) {
    auto text = read_text(path);
    if (!text) {
        return std::unexpected(text.error());
    }
    return analyze_source_text(*text, parse_module_syntax);
}

std::expected<SourceIndex, Error> build_source_index(
    const fs::path& root,
    const std::unordered_set<std::string>& excluded_directory_keys) {
    SourceIndex result;
    if (auto root_snapshot = snapshot_path(root, true)) {
        result.directory_snapshots.push_back(std::move(*root_snapshot));
    } else {
        result.cacheable = false;
    }

    std::error_code error_code;
    fs::recursive_directory_iterator iterator{
        root,
        fs::directory_options::skip_permission_denied,
        error_code};
    const fs::recursive_directory_iterator end;
    if (error_code) {
        return std::unexpected(failure(
            ErrorCode::enumeration_failed,
            root,
            "failed to enumerate source discovery project root"));
    }

    for (; iterator != end; iterator.increment(error_code)) {
        if (error_code) {
            return std::unexpected(failure(
                ErrorCode::enumeration_failed,
                root,
                "failed while enumerating source discovery project root: "
                    + error_code.message()));
        }

        const fs::directory_entry& item = *iterator;
        if (item.is_directory(error_code)) {
            const bool configured_excluded = !error_code
                && excluded_directory_keys.contains(path_key(item.path()));
            const bool excluded = !error_code
                && (default_excluded_directory(item.path()) || configured_excluded);
            if (excluded) {
                iterator.disable_recursion_pending();
            } else if (!error_code) {
                fs::path absolute = fs::absolute(item.path(), error_code).lexically_normal();
                if (!error_code) {
                    if (auto snapshot = snapshot_path(absolute, true)) {
                        result.directory_snapshots.push_back(std::move(*snapshot));
                    } else {
                        result.cacheable = false;
                    }
                } else {
                    result.cacheable = false;
                }
            }
            error_code.clear();
            continue;
        }
        error_code.clear();
        if (!item.is_regular_file(error_code) || error_code || !indexed_path(item.path())) {
            error_code.clear();
            continue;
        }

        IndexedFile record;
        record.path = fs::absolute(item.path(), error_code).lexically_normal();
        if (error_code) {
            error_code.clear();
            result.cacheable = false;
            continue;
        }
        if (auto snapshot = snapshot_path(record.path, false)) {
            result.file_snapshots.push_back(std::move(*snapshot));
        } else {
            result.cacheable = false;
        }

        record.translation_unit_kind = classify_translation_unit_path(record.path);
        record.kind = record.translation_unit_kind
            ? IndexedFileKind::translation_unit
            : IndexedFileKind::header;

        auto analysis = read_source_analysis(
            record.path,
            record.kind == IndexedFileKind::translation_unit
                && is_cpp_translation_unit_path(record.path));
        if (analysis) {
            record.local_includes = std::move(analysis->local_includes);
            record.module_syntax = std::move(analysis->module_syntax);
            record.defines_main = record.kind == IndexedFileKind::translation_unit
                && record.translation_unit_kind == TranslationUnitKind::source
                && analysis->defines_main;
        } else {
            result.cacheable = false;
            result.warnings.push_back(Warning{
                .code = WarningCode::file_read_failed,
                .path = record.path,
                .message = analysis.error(),
            });
        }

        const std::size_t index = result.files.size();
        result.index_by_path.emplace(path_key(record.path), index);
        result.files.push_back(std::move(record));
    }

    seal_snapshots(result.file_snapshots, false, result.cacheable);
    seal_snapshots(result.directory_snapshots, true, result.cacheable);
    return result;
}

} // namespace mqb::discovery::detail
