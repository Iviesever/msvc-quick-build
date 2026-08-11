#include "mqb/discovery/SourceDiscovery.hpp"

#include <algorithm>
#include <cctype>
#include <deque>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "mqb/core/TranslationUnitClassifier.hpp"
#include "mqb/discovery/ModuleSyntax.hpp"

namespace mqb::discovery {
namespace {

namespace fs = std::filesystem;

struct FileRecord {
    fs::path path;
    enum class Kind { translation_unit, header } kind{Kind::header};
    std::optional<TranslationUnitKind> translation_unit_kind;
    std::vector<std::string> local_includes;
    NamedModuleSyntax module_syntax;
    bool defines_main{false};
};

struct IndexedProject {
    std::vector<FileRecord> files;
    std::unordered_map<std::string, std::vector<std::size_t>> by_name;
};

[[nodiscard]] Error failure(
    const ErrorCode code,
    const fs::path& path,
    std::string message) {
    return Error{
        .code = code,
        .path = path,
        .message = std::move(message),
    };
}

[[nodiscard]] std::string path_key(const fs::path& path) {
    auto normalized = path.lexically_normal().generic_u8string();
#ifdef _WIN32
    std::string key;
    key.reserve(normalized.size());
    for (const char8_t byte : normalized) {
        const unsigned char ch = static_cast<unsigned char>(byte);
        key.push_back(static_cast<char>(std::tolower(ch)));
    }
    return key;
#else
    return std::string{
        reinterpret_cast<const char*>(normalized.data()),
        normalized.size()};
#endif
}

[[nodiscard]] bool same_path(const fs::path& left, const fs::path& right) {
    return path_key(left) == path_key(right);
}

[[nodiscard]] bool safe_relative(const fs::path& relative) {
    if (relative.empty() || relative.is_absolute() || relative == ".") {
        return false;
    }
    for (const auto& component : relative) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool within_root(const fs::path& root, const fs::path& path) {
    const fs::path relative = path.lexically_normal().lexically_relative(root.lexically_normal());
    return safe_relative(relative);
}

[[nodiscard]] bool built_in_excluded_name(const std::string& name) {
    static const std::unordered_set<std::string> exact{
        ".git", ".mqb", ".vs", "build", "out", "dist", ".cache", "node_modules"};
    if (exact.contains(name)) {
        return true;
    }
    return name.starts_with("cmake-build-");
}

[[nodiscard]] bool built_in_excluded_directory(const fs::path& path) {
    return built_in_excluded_name(path.filename().string());
}

[[nodiscard]] std::expected<fs::path, Error> normalize_existing_path(
    const fs::path& path,
    const ErrorCode error_code,
    const std::string_view description) {
    std::error_code ec;
    fs::path absolute = fs::absolute(path, ec);
    if (ec) {
        return std::unexpected(failure(
            error_code,
            path,
            "failed to resolve " + std::string{description} + ": " + ec.message()));
    }
    absolute = absolute.lexically_normal();
    if (!fs::exists(absolute, ec) || ec) {
        return std::unexpected(failure(
            error_code,
            absolute,
            std::string{description} + " does not exist"));
    }
    return absolute;
}

[[nodiscard]] std::expected<fs::path, Error> normalize_source_correction(
    const fs::path& root,
    const fs::path& path,
    const std::string_view description) {
    auto normalized = normalize_existing_path(path, ErrorCode::invalid_correction, description);
    if (!normalized) return std::unexpected(normalized.error());

    std::error_code ec;
    if (!fs::is_regular_file(*normalized, ec) || ec) {
        return std::unexpected(failure(
            ErrorCode::invalid_correction,
            *normalized,
            std::string{description} + " must be a regular file"));
    }
    if (!within_root(root, *normalized)) {
        return std::unexpected(failure(
            ErrorCode::invalid_correction,
            *normalized,
            std::string{description} + " must be inside the project root"));
    }
    if (!is_translation_unit_path(*normalized)) {
        return std::unexpected(failure(
            ErrorCode::invalid_correction,
            *normalized,
            std::string{description} + " must use a supported C/C++ translation-unit extension"));
    }
    return *normalized;
}

[[nodiscard]] std::expected<fs::path, Error> normalize_excluded_directory(
    const fs::path& root,
    const fs::path& path) {
    auto normalized = normalize_existing_path(
        path,
        ErrorCode::invalid_correction,
        "excluded directory");
    if (!normalized) return std::unexpected(normalized.error());

    std::error_code ec;
    if (!fs::is_directory(*normalized, ec) || ec) {
        return std::unexpected(failure(
            ErrorCode::invalid_correction,
            *normalized,
            "excluded directory must be a directory"));
    }
    if (!within_root(root, *normalized)) {
        return std::unexpected(failure(
            ErrorCode::invalid_correction,
            *normalized,
            "excluded directory must be inside the project root"));
    }
    return *normalized;
}

[[nodiscard]] bool explicitly_excluded_directory(
    const fs::path& path,
    const std::vector<fs::path>& excluded_directories) {
    return std::any_of(
        excluded_directories.begin(),
        excluded_directories.end(),
        [&path](const fs::path& excluded) { return same_path(path, excluded); });
}

[[nodiscard]] bool under_excluded_directory(
    const fs::path& path,
    const std::vector<fs::path>& excluded_directories) {
    return std::any_of(
        excluded_directories.begin(),
        excluded_directories.end(),
        [&path](const fs::path& excluded) {
            return same_path(path, excluded) || within_root(excluded, path);
        });
}

[[nodiscard]] bool indexed_path(const fs::path& path) {
    if (is_translation_unit_path(path)) {
        return true;
    }
    const std::string extension = path.extension().string();
    std::string lowered;
    lowered.reserve(extension.size());
    for (const unsigned char ch : extension) {
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    }
    return lowered == ".h" || lowered == ".hh" || lowered == ".hpp" || lowered == ".hxx";
}

[[nodiscard]] std::expected<std::string, Error> read_text(const fs::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return std::unexpected(failure(
            ErrorCode::read_failed,
            path,
            "failed to open source file"));
    }
    std::string text{
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{}};
    if (!stream.eof() && stream.fail()) {
        return std::unexpected(failure(
            ErrorCode::read_failed,
            path,
            "failed while reading source file"));
    }
    return text;
}

[[nodiscard]] std::string strip_comments_and_literals(const std::string_view text) {
    enum class State { normal, line_comment, block_comment, string_literal, character_literal };
    State state = State::normal;
    bool escaped = false;
    std::string result{text};

    for (std::size_t index = 0; index < text.size(); ++index) {
        const char ch = text[index];
        const char next = index + 1 < text.size() ? text[index + 1] : '\0';
        switch (state) {
        case State::normal:
            if (ch == '/' && next == '/') {
                result[index] = ' ';
                result[index + 1] = ' ';
                ++index;
                state = State::line_comment;
            } else if (ch == '/' && next == '*') {
                result[index] = ' ';
                result[index + 1] = ' ';
                ++index;
                state = State::block_comment;
            } else if (ch == '"') {
                result[index] = ' ';
                state = State::string_literal;
                escaped = false;
            } else if (ch == '\'') {
                result[index] = ' ';
                state = State::character_literal;
                escaped = false;
            }
            break;
        case State::line_comment:
            if (ch == '\n') state = State::normal;
            else result[index] = ' ';
            break;
        case State::block_comment:
            if (ch == '*' && next == '/') {
                result[index] = ' ';
                result[index + 1] = ' ';
                ++index;
                state = State::normal;
            } else if (ch != '\n') {
                result[index] = ' ';
            }
            break;
        case State::string_literal:
            if (ch == '\n') {
                state = State::normal;
                escaped = false;
                break;
            }
            result[index] = ' ';
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') state = State::normal;
            break;
        case State::character_literal:
            if (ch == '\n') {
                state = State::normal;
                escaped = false;
                break;
            }
            result[index] = ' ';
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '\'') state = State::normal;
            break;
        }
    }
    return result;
}

[[nodiscard]] std::vector<std::string> parse_local_includes(const std::string_view text) {
    std::vector<std::string> includes;
    std::size_t position = 0;
    while (position < text.size()) {
        const std::size_t end = text.find('\n', position);
        const std::string_view line = text.substr(
            position,
            end == std::string_view::npos ? text.size() - position : end - position);
        std::size_t cursor = 0;
        while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor]))) ++cursor;
        if (cursor < line.size() && line[cursor] == '#') {
            ++cursor;
            while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor]))) ++cursor;
            constexpr std::string_view include_keyword = "include";
            if (line.substr(cursor).starts_with(include_keyword)) {
                cursor += include_keyword.size();
                while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor]))) ++cursor;
                if (cursor < line.size() && line[cursor] == '"') {
                    ++cursor;
                    const std::size_t close = line.find('"', cursor);
                    if (close != std::string_view::npos && close > cursor) {
                        includes.emplace_back(line.substr(cursor, close - cursor));
                    }
                }
            }
        }
        if (end == std::string_view::npos) break;
        position = end + 1;
    }
    return includes;
}

[[nodiscard]] bool identifier_boundary(const char ch) {
    const unsigned char byte = static_cast<unsigned char>(ch);
    return !std::isalnum(byte) && ch != '_';
}

[[nodiscard]] bool contains_main(const std::string_view text) {
    std::size_t position = 0;
    while ((position = text.find("main", position)) != std::string_view::npos) {
        const bool left_ok = position == 0 || identifier_boundary(text[position - 1]);
        const std::size_t after = position + 4;
        bool right_ok = after >= text.size() || identifier_boundary(text[after]);
        if (left_ok && right_ok) {
            std::size_t cursor = after;
            while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor]))) ++cursor;
            if (cursor < text.size() && text[cursor] == '(') return true;
        }
        position = after;
    }
    return false;
}

[[nodiscard]] std::expected<FileRecord, Error> scan_file(const fs::path& path) {
    auto text = read_text(path);
    if (!text) return std::unexpected(text.error());

    FileRecord record;
    record.path = path;
    record.translation_unit_kind = classify_translation_unit_path(path);
    record.kind = record.translation_unit_kind
        ? FileRecord::Kind::translation_unit
        : FileRecord::Kind::header;
    const std::string comment_free = strip_comments_and_literals(*text);
    record.local_includes = parse_local_includes(comment_free);
    // C participates in ordinary include/main discovery, but C++ module lexical
    // syntax is not meaningful in C. Keep headers and C++ TUs unchanged while
    // preventing legal C identifiers such as `module` / `import` from routing a
    // target into the P1689 pipeline.
    if (!record.translation_unit_kind || is_cpp_translation_unit_path(path)) {
        record.module_syntax = ModuleSyntaxParser::parse(*text);
    }
    record.defines_main = record.kind == FileRecord::Kind::translation_unit
        && contains_main(comment_free);
    return record;
}

[[nodiscard]] std::expected<IndexedProject, Error> index_project(
    const fs::path& root,
    const std::vector<fs::path>& excluded_directories) {
    IndexedProject index;
    std::error_code ec;
    fs::recursive_directory_iterator iterator{
        root,
        fs::directory_options::skip_permission_denied,
        ec};
    const fs::recursive_directory_iterator end;
    if (ec) {
        return std::unexpected(failure(
            ErrorCode::read_failed,
            root,
            "failed to start source discovery: " + ec.message()));
    }

    for (; iterator != end; iterator.increment(ec)) {
        if (ec) {
            const fs::path failed_path = iterator == end ? root : iterator->path();
            return std::unexpected(failure(
                ErrorCode::read_failed,
                failed_path,
                "failed while enumerating project files: " + ec.message()));
        }

        const fs::path path = iterator->path().lexically_normal();
        if (iterator->is_directory(ec)) {
            if (ec) {
                return std::unexpected(failure(
                    ErrorCode::read_failed,
                    path,
                    "failed to inspect project directory: " + ec.message()));
            }
            if (built_in_excluded_directory(path)
                || explicitly_excluded_directory(path, excluded_directories)) {
                iterator.disable_recursion_pending();
            }
            continue;
        }
        if (ec) {
            return std::unexpected(failure(
                ErrorCode::read_failed,
                path,
                "failed to inspect project file: " + ec.message()));
        }
        if (!iterator->is_regular_file(ec) || ec || !indexed_path(path)) continue;

        auto record = scan_file(path);
        if (!record) return std::unexpected(record.error());
        const std::size_t id = index.files.size();
        index.by_name[path.filename().string()].push_back(id);
        index.files.push_back(std::move(*record));
    }
    return index;
}

[[nodiscard]] std::optional<std::size_t> find_record(
    const IndexedProject& index,
    const fs::path& path) {
    for (std::size_t id = 0; id < index.files.size(); ++id) {
        if (same_path(index.files[id].path, path)) return id;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t> resolve_local_include(
    const IndexedProject& index,
    const FileRecord& from,
    const std::string& include,
    const std::vector<fs::path>& include_directories) {
    const fs::path local = (from.path.parent_path() / include).lexically_normal();
    if (auto id = find_record(index, local)) return id;

    for (const auto& directory : include_directories) {
        const fs::path candidate = (directory / include).lexically_normal();
        if (auto id = find_record(index, candidate)) return id;
    }

    const fs::path include_name = fs::path{include}.filename();
    if (const auto it = index.by_name.find(include_name.string()); it != index.by_name.end()) {
        if (it->second.size() == 1) return it->second.front();
    }
    return std::nullopt;
}

[[nodiscard]] std::string stem_key(const fs::path& path) {
    std::string stem = path.stem().string();
#ifdef _WIN32
    std::transform(
        stem.begin(), stem.end(), stem.begin(),
        [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
#endif
    return stem;
}

[[nodiscard]] bool module_interface_translation_unit(const FileRecord& file) {
    return file.kind == FileRecord::Kind::translation_unit
        && file.translation_unit_kind == TranslationUnitKind::module_interface;
}

[[nodiscard]] bool file_requires_module_pipeline(const FileRecord& file) {
    if (is_c_translation_unit_path(file.path)) {
        return false;
    }
    return module_interface_translation_unit(file)
        || file.module_syntax.declared_module.has_value()
        || !file.module_syntax.imported_modules.empty()
        || file.module_syntax.has_header_unit_import;
}

struct EdgeGraph {
    std::vector<std::vector<std::size_t>> edges;
    std::vector<std::string> warnings;
};

[[nodiscard]] EdgeGraph build_edges(
    const IndexedProject& index,
    const std::vector<fs::path>& include_directories) {
    EdgeGraph graph;
    graph.edges.resize(index.files.size());
    for (std::size_t id = 0; id < index.files.size(); ++id) {
        for (const auto& include : index.files[id].local_includes) {
            const auto target = resolve_local_include(
                index,
                index.files[id],
                include,
                include_directories);
            if (target) {
                graph.edges[id].push_back(*target);
            }
        }
        std::sort(graph.edges[id].begin(), graph.edges[id].end());
        graph.edges[id].erase(
            std::unique(graph.edges[id].begin(), graph.edges[id].end()),
            graph.edges[id].end());
    }

    std::unordered_map<std::string, std::vector<std::size_t>> by_stem;
    for (std::size_t id = 0; id < index.files.size(); ++id) {
        by_stem[stem_key(index.files[id].path)].push_back(id);
    }
    for (const auto& [stem, ids] : by_stem) {
        (void)stem;
        std::vector<std::size_t> sources;
        std::vector<std::size_t> headers;
        for (const auto id : ids) {
            if (index.files[id].kind == FileRecord::Kind::translation_unit) sources.push_back(id);
            else headers.push_back(id);
        }
        if (sources.size() == 1) {
            for (const auto header : headers) {
                graph.edges[sources.front()].push_back(header);
                graph.edges[header].push_back(sources.front());
            }
        }
    }
    for (auto& neighbors : graph.edges) {
        std::sort(neighbors.begin(), neighbors.end());
        neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    }
    return graph;
}

[[nodiscard]] std::vector<bool> reachable_from(
    const EdgeGraph& graph,
    const std::size_t entry) {
    std::vector<bool> reachable(graph.edges.size(), false);
    std::deque<std::size_t> queue;
    queue.push_back(entry);
    reachable[entry] = true;
    while (!queue.empty()) {
        const auto current = queue.front();
        queue.pop_front();
        for (const auto neighbor : graph.edges[current]) {
            if (!reachable[neighbor]) {
                reachable[neighbor] = true;
                queue.push_back(neighbor);
            }
        }
    }
    return reachable;
}

void add_edge(EdgeGraph& graph, const std::size_t from, const std::size_t to) {
    auto& edges = graph.edges[from];
    if (std::find(edges.begin(), edges.end(), to) == edges.end()) {
        edges.push_back(to);
    }
}

void add_reachable_module_provider_edges(
    const IndexedProject& index,
    EdgeGraph& graph,
    std::vector<bool>& reachable) {
    std::map<std::string, std::vector<std::size_t>, std::less<>> providers;
    for (std::size_t id = 0; id < index.files.size(); ++id) {
        const auto& file = index.files[id];
        if (module_interface_translation_unit(file)
            && file.module_syntax.declared_module) {
            providers[*file.module_syntax.declared_module].push_back(id);
        }
    }

    std::deque<std::size_t> pending;
    std::vector<bool> scanned(index.files.size(), false);
    for (std::size_t id = 0; id < reachable.size(); ++id) {
        if (reachable[id]) pending.push_back(id);
    }

    while (!pending.empty()) {
        const std::size_t importer = pending.front();
        pending.pop_front();
        if (scanned[importer]) continue;
        scanned[importer] = true;

        for (const auto& logical_name : index.files[importer].module_syntax.imported_modules) {
            const auto found = providers.find(logical_name);
            if (found == providers.end() || found->second.size() != 1) continue;

            const std::size_t provider = found->second.front();
            add_edge(graph, importer, provider);
            add_edge(graph, provider, importer);
            if (!reachable[provider]) {
                reachable[provider] = true;
                pending.push_back(provider);
            }
        }
    }
}

[[nodiscard]] std::optional<std::size_t> find_header_owner(
    const IndexedProject& index,
    const EdgeGraph& graph,
    const std::size_t header) {
    std::vector<std::size_t> candidates;
    for (const auto neighbor : graph.edges[header]) {
        if (index.files[neighbor].kind == FileRecord::Kind::translation_unit) {
            candidates.push_back(neighbor);
        }
    }
    if (candidates.size() == 1) return candidates.front();
    return std::nullopt;
}

void select_translation_units(
    const IndexedProject& index,
    const EdgeGraph& graph,
    const std::vector<bool>& reachable,
    std::set<std::size_t>& selected) {
    for (std::size_t id = 0; id < index.files.size(); ++id) {
        if (reachable[id] && index.files[id].kind == FileRecord::Kind::translation_unit) {
            selected.insert(id);
        }
    }
    for (std::size_t id = 0; id < index.files.size(); ++id) {
        if (!reachable[id] || index.files[id].kind != FileRecord::Kind::header) continue;
        if (auto owner = find_header_owner(index, graph, id)) selected.insert(*owner);
    }
}

[[nodiscard]] std::vector<fs::path> sorted_selected_paths(
    const IndexedProject& index,
    const std::set<std::size_t>& selected,
    const fs::path& entry) {
    std::vector<fs::path> paths;
    paths.reserve(selected.size());
    for (const auto id : selected) paths.push_back(index.files[id].path);
    std::sort(paths.begin(), paths.end(), [&entry](const fs::path& left, const fs::path& right) {
        if (same_path(left, entry)) return true;
        if (same_path(right, entry)) return false;
        return path_key(left) < path_key(right);
    });
    return paths;
}

} // namespace

std::expected<Result, Error> SourceDiscovery::discover(const Request& request) {
    auto root = normalize_existing_path(
        request.project_root,
        ErrorCode::invalid_root,
        "project root");
    if (!root) return std::unexpected(root.error());
    auto entry = normalize_existing_path(
        request.entry,
        ErrorCode::invalid_entry,
        "entry source");
    if (!entry) return std::unexpected(entry.error());

    std::error_code ec;
    if (!fs::is_directory(*root, ec) || ec) {
        return std::unexpected(failure(
            ErrorCode::invalid_root,
            *root,
            "project root must be a directory"));
    }
    if (!fs::is_regular_file(*entry, ec) || ec || !is_translation_unit_path(*entry)) {
        return std::unexpected(failure(
            ErrorCode::invalid_entry,
            *entry,
            "entry source must be a supported C/C++ translation unit"));
    }
    if (!within_root(*root, *entry)) {
        return std::unexpected(failure(
            ErrorCode::entry_outside_root,
            *entry,
            "entry source must be inside the project root"));
    }

    std::vector<fs::path> excluded_directories;
    excluded_directories.reserve(request.excluded_directories.size());
    for (const auto& directory : request.excluded_directories) {
        auto normalized = normalize_excluded_directory(*root, directory);
        if (!normalized) return std::unexpected(normalized.error());
        excluded_directories.push_back(std::move(*normalized));
    }
    if (under_excluded_directory(*entry, excluded_directories)) {
        return std::unexpected(failure(
            ErrorCode::invalid_correction,
            *entry,
            "entry source must not be inside an excluded directory"));
    }

    std::vector<fs::path> extra_sources;
    extra_sources.reserve(request.extra_sources.size());
    for (const auto& source : request.extra_sources) {
        auto normalized = normalize_source_correction(*root, source, "extra source");
        if (!normalized) return std::unexpected(normalized.error());
        if (under_excluded_directory(*normalized, excluded_directories)) {
            return std::unexpected(failure(
                ErrorCode::invalid_correction,
                *normalized,
                "extra source must not be inside an excluded directory"));
        }
        extra_sources.push_back(std::move(*normalized));
    }

    std::vector<fs::path> excluded_sources;
    excluded_sources.reserve(request.excluded_sources.size());
    for (const auto& source : request.excluded_sources) {
        auto normalized = normalize_source_correction(*root, source, "excluded source");
        if (!normalized) return std::unexpected(normalized.error());
        if (same_path(*normalized, *entry)) {
            return std::unexpected(failure(
                ErrorCode::invalid_correction,
                *normalized,
                "entry source must not be excluded"));
        }
        if (under_excluded_directory(*normalized, excluded_directories)) {
            return std::unexpected(failure(
                ErrorCode::invalid_correction,
                *normalized,
                "excluded source is redundant because its directory is already excluded"));
        }
        excluded_sources.push_back(std::move(*normalized));
    }

    for (const auto& extra : extra_sources) {
        if (std::any_of(
                excluded_sources.begin(),
                excluded_sources.end(),
                [&extra](const fs::path& excluded) { return same_path(extra, excluded); })) {
            return std::unexpected(failure(
                ErrorCode::invalid_correction,
                extra,
                "source cannot be both extra and excluded"));
        }
    }

    auto index = index_project(*root, excluded_directories);
    if (!index) return std::unexpected(index.error());

    const auto entry_id = find_record(*index, *entry);
    if (!entry_id) {
        return std::unexpected(failure(
            ErrorCode::invalid_entry,
            *entry,
            "entry source was not indexed"));
    }

    for (const auto& source : extra_sources) {
        if (!find_record(*index, source)) {
            return std::unexpected(failure(
                ErrorCode::invalid_correction,
                source,
                "extra source was not indexed"));
        }
    }
    for (const auto& source : excluded_sources) {
        if (!find_record(*index, source)) {
            return std::unexpected(failure(
                ErrorCode::invalid_correction,
                source,
                "excluded source was not indexed"));
        }
    }

    std::vector<fs::path> normalized_include_directories;
    normalized_include_directories.reserve(request.include_directories.size());
    for (const auto& directory : request.include_directories) {
        std::error_code include_error;
        fs::path absolute = fs::absolute(directory, include_error);
        if (include_error) {
            return std::unexpected(failure(
                ErrorCode::invalid_root,
                directory,
                "failed to resolve include directory"));
        }
        normalized_include_directories.push_back(absolute.lexically_normal());
    }

    auto graph = build_edges(*index, normalized_include_directories);

    std::vector<bool> blocked(index->files.size(), false);
    for (const auto& source : excluded_sources) {
        if (const auto id = find_record(*index, source)) blocked[*id] = true;
    }
    for (std::size_t from = 0; from < graph.edges.size(); ++from) {
        auto& edges = graph.edges[from];
        edges.erase(
            std::remove_if(
                edges.begin(),
                edges.end(),
                [&blocked](const std::size_t target) { return blocked[target]; }),
            edges.end());
        if (blocked[from]) edges.clear();
    }

    auto reachable = reachable_from(graph, *entry_id);
    add_reachable_module_provider_edges(*index, graph, reachable);
    reachable = reachable_from(graph, *entry_id);
    std::set<std::size_t> selected;
    select_translation_units(*index, graph, reachable, selected);
    selected.insert(*entry_id);

    for (const auto& extra : extra_sources) {
        if (const auto id = find_record(*index, extra)) selected.insert(*id);
    }
    for (const auto& excluded : excluded_sources) {
        if (const auto id = find_record(*index, excluded)) selected.erase(*id);
    }

    for (const auto id : selected) {
        if (id != *entry_id && index->files[id].defines_main) {
            return std::unexpected(failure(
                ErrorCode::ambiguous_main,
                index->files[id].path,
                "discovered source defines another main()"));
        }
    }

    Result result;
    result.sources = sorted_selected_paths(*index, selected, *entry);
    result.indexed_files = index->files.size();
    result.requires_module_pipeline = std::any_of(
        selected.begin(),
        selected.end(),
        [&index](const std::size_t id) {
            return file_requires_module_pipeline(index->files[id]);
        });
    result.warnings.reserve(graph.warnings.size());
    for (auto& warning : graph.warnings) {
        result.warnings.push_back(Warning{.message = std::move(warning)});
    }
    return result;
}

} // namespace mqb::discovery
