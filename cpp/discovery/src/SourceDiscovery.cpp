#include "mqb/discovery/SourceDiscovery.hpp"

#include <algorithm>
#include <cctype>
#include <deque>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "mqb/core/TranslationUnitClassifier.hpp"
#include "mqb/discovery/ModuleSyntax.hpp"

namespace mqb::discovery {
namespace {

namespace fs = std::filesystem;

enum class FileKind {
    translation_unit,
    header,
};

struct FileRecord {
    fs::path path;
    FileKind kind{FileKind::header};
    std::optional<TranslationUnitKind> translation_unit_kind;
    std::vector<std::string> local_includes;
    NamedModuleSyntax module_syntax;
    bool defines_main{false};
};

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

[[nodiscard]] std::string ascii_lower(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

[[nodiscard]] std::string path_key(const fs::path& path) {
    return ascii_lower(path.lexically_normal().generic_string());
}

[[nodiscard]] std::string extension_lower(const fs::path& path) {
    return ascii_lower(path.extension().string());
}

[[nodiscard]] bool header_extension(const fs::path& path) {
    const std::string extension = extension_lower(path);
    return extension == ".h"
        || extension == ".hh"
        || extension == ".hpp"
        || extension == ".hxx"
        || extension == ".inl"
        || extension == ".ipp";
}

[[nodiscard]] bool indexed_path(const fs::path& path) {
    return is_translation_unit_path(path) || header_extension(path);
}

[[nodiscard]] bool default_excluded_directory(const fs::path& path) {
    const std::string name = ascii_lower(path.filename().string());
    return name == ".mqb"
        || name == ".git"
        || name == ".vs"
        || name == "build"
        || name == "out"
        || name.starts_with("cmake-build-");
}

[[nodiscard]] bool inside_root(const fs::path& root, const fs::path& path) {
    const fs::path relative = path.lexically_normal().lexically_relative(root.lexically_normal());
    if (relative.empty() || relative.is_absolute()) {
        return false;
    }
    for (const auto& component : relative) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool same_or_inside(const fs::path& root, const fs::path& path) {
    return path.lexically_normal() == root.lexically_normal() || inside_root(root, path);
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

[[nodiscard]] std::string strip_comments_preserve_literals(const std::string_view input) {
    enum class State {
        normal,
        line_comment,
        block_comment,
        string_literal,
        char_literal,
    };

    State state = State::normal;
    bool escaped = false;
    std::string output;
    output.reserve(input.size());

    for (std::size_t index = 0; index < input.size(); ++index) {
        const char ch = input[index];
        const char next = index + 1 < input.size() ? input[index + 1] : '\0';

        switch (state) {
        case State::normal:
            if (ch == '/' && next == '/') {
                output.append("  ");
                ++index;
                state = State::line_comment;
            } else if (ch == '/' && next == '*') {
                output.append("  ");
                ++index;
                state = State::block_comment;
            } else {
                output.push_back(ch);
                if (ch == '"') {
                    state = State::string_literal;
                    escaped = false;
                } else if (ch == '\'') {
                    state = State::char_literal;
                    escaped = false;
                }
            }
            break;

        case State::line_comment:
            if (ch == '\n') {
                output.push_back('\n');
                state = State::normal;
            } else {
                output.push_back(' ');
            }
            break;

        case State::block_comment:
            if (ch == '*' && next == '/') {
                output.append("  ");
                ++index;
                state = State::normal;
            } else {
                output.push_back(ch == '\n' ? '\n' : ' ');
            }
            break;

        case State::string_literal:
        case State::char_literal: {
            output.push_back(ch);
            const char closing = state == State::string_literal ? '"' : '\'';
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == closing) {
                state = State::normal;
            }
            break;
        }
        }
    }
    return output;
}

[[nodiscard]] std::string blank_literals(const std::string_view input) {
    enum class State {
        normal,
        string_literal,
        char_literal,
    };
    State state = State::normal;
    bool escaped = false;
    std::string output;
    output.reserve(input.size());

    for (const char ch : input) {
        switch (state) {
        case State::normal:
            if (ch == '"') {
                output.push_back(' ');
                state = State::string_literal;
                escaped = false;
            } else if (ch == '\'') {
                output.push_back(' ');
                state = State::char_literal;
                escaped = false;
            } else {
                output.push_back(ch);
            }
            break;
        case State::string_literal:
        case State::char_literal: {
            output.push_back(ch == '\n' ? '\n' : ' ');
            const char closing = state == State::string_literal ? '"' : '\'';
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == closing) {
                state = State::normal;
            }
            break;
        }
        }
    }
    return output;
}

[[nodiscard]] std::vector<std::string>
parse_local_includes(const std::string_view comment_free) {
    std::vector<std::string> includes;
    std::size_t line_begin = 0;
    while (line_begin <= comment_free.size()) {
        const std::size_t newline = comment_free.find('\n', line_begin);
        const std::size_t line_end = newline == std::string_view::npos
            ? comment_free.size()
            : newline;
        std::string_view line = comment_free.substr(line_begin, line_end - line_begin);

        std::size_t cursor = 0;
        while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor]))) {
            ++cursor;
        }
        if (cursor < line.size() && line[cursor] == '#') {
            ++cursor;
            while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor]))) {
                ++cursor;
            }
            constexpr std::string_view keyword = "include";
            if (line.substr(cursor, keyword.size()) == keyword) {
                cursor += keyword.size();
                if (cursor == line.size()
                    || !std::isalnum(static_cast<unsigned char>(line[cursor]))) {
                    while (cursor < line.size()
                           && std::isspace(static_cast<unsigned char>(line[cursor]))) {
                        ++cursor;
                    }
                    if (cursor < line.size() && line[cursor] == '"') {
                        const std::size_t end_quote = line.find('"', cursor + 1);
                        if (end_quote != std::string_view::npos && end_quote > cursor + 1) {
                            includes.emplace_back(line.substr(cursor + 1, end_quote - cursor - 1));
                        }
                    }
                }
            }
        }

        if (newline == std::string_view::npos) {
            break;
        }
        line_begin = newline + 1;
    }
    return includes;
}

[[nodiscard]] bool contains_main(const std::string_view comment_free) {
    const std::string code = blank_literals(comment_free);
    std::size_t cursor = 0;
    while ((cursor = code.find("main", cursor)) != std::string::npos) {
        const bool left_boundary = cursor == 0
            || !(std::isalnum(static_cast<unsigned char>(code[cursor - 1]))
                 || code[cursor - 1] == '_');
        const std::size_t after_name = cursor + 4;
        const bool right_boundary = after_name >= code.size()
            || !(std::isalnum(static_cast<unsigned char>(code[after_name]))
                 || code[after_name] == '_');
        if (left_boundary && right_boundary) {
            std::size_t next = after_name;
            while (next < code.size() && std::isspace(static_cast<unsigned char>(code[next]))) {
                ++next;
            }
            if (next < code.size() && code[next] == '(') {
                return true;
            }
        }
        cursor = after_name;
    }
    return false;
}

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

[[nodiscard]] std::expected<fs::path, Error> normalize_directory_correction(
    const fs::path& requested,
    const fs::path& root) {
    std::error_code error_code;
    fs::path absolute = fs::absolute(requested, error_code).lexically_normal();
    if (error_code
        || !fs::is_directory(absolute, error_code)
        || error_code
        || !inside_root(root, absolute)) {
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
        || !inside_root(root, absolute)) {
        return std::unexpected(failure(
            ErrorCode::invalid_correction,
            requested,
            std::string{description}
                + " must be an existing supported C++ translation unit inside the project root"));
    }
    return absolute;
}

[[nodiscard]] bool ordinary_translation_unit(const FileRecord& file) {
    return file.kind == FileKind::translation_unit
        && file.translation_unit_kind == TranslationUnitKind::source;
}

[[nodiscard]] bool module_interface_translation_unit(const FileRecord& file) {
    return file.kind == FileKind::translation_unit
        && file.translation_unit_kind == TranslationUnitKind::module_interface;
}

[[nodiscard]] std::string ownership_key(const fs::path& path) {
    return path_key(path.parent_path() / path.stem());
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
        || !inside_root(root, entry)) {
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
        if (!directory) return std::unexpected(directory.error());
        if (same_or_inside(*directory, entry)) {
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
        if (!source) return std::unexpected(source.error());
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
        if (!source) return std::unexpected(source.error());
        const std::string key = path_key(*source);
        if (excluded_source_keys.contains(key)) {
            return std::unexpected(failure(
                ErrorCode::invalid_correction,
                *source,
                "the same source cannot be both extra and excluded"));
        }
        for (const auto& directory : excluded_directories) {
            if (same_or_inside(directory, *source)) {
                return std::unexpected(failure(
                    ErrorCode::invalid_correction,
                    *source,
                    "discovery extra source must not be inside an excluded directory"));
            }
        }
        if (*source != entry && extra_source_keys.insert(key).second) {
            auto text = read_text(*source);
            if (!text) {
                return std::unexpected(failure(
                    ErrorCode::invalid_correction,
                    *source,
                    text.error()));
            }
            const auto source_kind = classify_translation_unit_path(*source);
            const std::string comment_free = strip_comments_preserve_literals(*text);
            if (source_kind == TranslationUnitKind::source && contains_main(comment_free)) {
                return std::unexpected(failure(
                    ErrorCode::invalid_correction,
                    *source,
                    "discovery extra source must not define another main()"));
            }
            extra_sources.push_back(std::move(*source));
        }
    }

    Result result;
    std::vector<FileRecord> files;
    std::unordered_map<std::string, std::size_t> index_by_path;

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
                "failed while enumerating source discovery project root: " + error_code.message()));
        }
        const fs::directory_entry& item = *iterator;
        if (item.is_directory(error_code)) {
            const bool configured_excluded = !error_code
                && excluded_directory_keys.contains(path_key(item.path()));
            if (!error_code && (default_excluded_directory(item.path()) || configured_excluded)) {
                iterator.disable_recursion_pending();
            }
            error_code.clear();
            continue;
        }
        error_code.clear();
        if (!item.is_regular_file(error_code) || error_code || !indexed_path(item.path())) {
            error_code.clear();
            continue;
        }

        FileRecord record;
        record.path = fs::absolute(item.path(), error_code).lexically_normal();
        if (error_code) {
            error_code.clear();
            continue;
        }
        record.translation_unit_kind = classify_translation_unit_path(record.path);
        record.kind = record.translation_unit_kind
            ? FileKind::translation_unit
            : FileKind::header;

        if (auto text = read_text(record.path)) {
            const std::string comment_free = strip_comments_preserve_literals(*text);
            record.local_includes = parse_local_includes(comment_free);
            if (record.kind == FileKind::translation_unit) {
                record.module_syntax = ModuleSyntaxParser::parse(*text);
                record.defines_main = ordinary_translation_unit(record)
                    && contains_main(comment_free);
            }
        } else {
            result.warnings.push_back(Warning{
                .code = WarningCode::file_read_failed,
                .path = record.path,
                .message = text.error(),
            });
        }

        const std::size_t index = files.size();
        index_by_path.emplace(path_key(record.path), index);
        files.push_back(std::move(record));
    }

    result.indexed_files = files.size();
    const auto entry_it = index_by_path.find(path_key(entry));
    if (entry_it == index_by_path.end()) {
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

    std::vector<std::vector<std::size_t>> adjacency(files.size());

    // Existing quoted-include connectivity remains unchanged and applies to
    // both ordinary and module translation units.
    for (std::size_t file_index = 0; file_index < files.size(); ++file_index) {
        const auto& file = files[file_index];
        for (const auto& include : file.local_includes) {
            std::vector<fs::path> candidates;
            candidates.reserve(1 + include_directories.size());
            candidates.push_back((file.path.parent_path() / include).lexically_normal());
            for (const auto& include_directory : include_directories) {
                candidates.push_back((include_directory / include).lexically_normal());
            }

            for (const auto& candidate : candidates) {
                const auto found = index_by_path.find(path_key(candidate));
                if (found != index_by_path.end()) {
                    add_undirected_edge(adjacency, file_index, found->second);
                    break;
                }
            }
        }
    }

    // Same-basename header ownership is intentionally restricted to ordinary
    // translation units. A module interface must be selected through module
    // syntax, not merely because it happens to share a header basename.
    std::unordered_map<std::string, std::vector<std::size_t>> ordinary_sources_by_owner;
    for (std::size_t index = 0; index < files.size(); ++index) {
        if (ordinary_translation_unit(files[index])) {
            ordinary_sources_by_owner[ownership_key(files[index].path)].push_back(index);
        }
    }
    for (std::size_t index = 0; index < files.size(); ++index) {
        if (files[index].kind != FileKind::header) continue;
        const auto owners = ordinary_sources_by_owner.find(ownership_key(files[index].path));
        if (owners == ordinary_sources_by_owner.end()) continue;
        for (const std::size_t owner : owners->second) {
            add_undirected_edge(adjacency, index, owner);
        }
    }

    // Named imports connect to every project-local interface candidate. We do
    // not choose a winner here: duplicate/ambiguous providers are deliberately
    // retained so the authoritative P1689 graph can diagnose them later.
    std::unordered_map<std::string, std::vector<std::size_t>> interface_providers;
    std::unordered_map<std::string, std::vector<std::size_t>> declared_module_units;
    for (std::size_t index = 0; index < files.size(); ++index) {
        if (files[index].kind != FileKind::translation_unit
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
        if (files[index].kind != FileKind::translation_unit) continue;
        for (const auto& imported : files[index].module_syntax.imported_modules) {
            const auto providers = interface_providers.find(imported);
            if (providers == interface_providers.end()) continue;
            for (const std::size_t provider : providers->second) {
                add_undirected_edge(adjacency, index, provider);
            }
        }
    }

    // A module implementation unit (`module M;`) contributes an object even
    // though it does not itself satisfy named import M. Connect all units that
    // declare the exact same logical module so selecting the interface also
    // selects its project-local implementation units.
    for (const auto& [logical_name, units] : declared_module_units) {
        static_cast<void>(logical_name);
        if (units.size() < 2) continue;
        const std::size_t first = units.front();
        for (std::size_t offset = 1; offset < units.size(); ++offset) {
            add_undirected_edge(adjacency, first, units[offset]);
        }
    }

    const std::size_t entry_index = entry_it->second;
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
            const bool excluded_source = files[next].kind == FileKind::translation_unit
                && excluded_source_keys.contains(path_key(files[next].path));
            if (!second_main && !excluded_source) {
                queue.push_back(next);
            }
        }
    }

    for (std::size_t index = 0; index < files.size(); ++index) {
        if (!visited[index] || files[index].kind != FileKind::translation_unit) {
            continue;
        }
        if (files[index].path != entry
            && ordinary_translation_unit(files[index])
            && files[index].defines_main) {
            continue;
        }
        if (excluded_source_keys.contains(path_key(files[index].path))) {
            continue;
        }
        result.sources.push_back(files[index].path);
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

    if (result.sources.empty() || result.sources.front() != entry) {
        return std::unexpected(failure(
            ErrorCode::invalid_entry,
            entry,
            "source discovery did not retain the entry translation unit"));
    }
    return result;
}

} // namespace mqb::discovery
