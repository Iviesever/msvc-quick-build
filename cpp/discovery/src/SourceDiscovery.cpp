#include "mqb/discovery/SourceDiscovery.hpp"

#include <algorithm>
#include <cctype>
#include <deque>
#include <expected>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mqb::discovery {
namespace {

namespace fs = std::filesystem;

enum class FileKind {
    source,
    header,
};

struct FileRecord {
    fs::path path;
    FileKind kind{FileKind::header};
    std::vector<std::string> local_includes;
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

[[nodiscard]] bool source_extension(const fs::path& path) {
    const std::string extension = extension_lower(path);
    return extension == ".cpp" || extension == ".cc" || extension == ".cxx";
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

[[nodiscard]] bool indexed_extension(const fs::path& path) {
    return source_extension(path) || header_extension(path);
}

[[nodiscard]] bool excluded_directory(const fs::path& path) {
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
        || !source_extension(entry)
        || !inside_root(root, entry)) {
        return std::unexpected(failure(
            ErrorCode::invalid_entry,
            request.entry,
            "source discovery entry must be an ordinary C++ source inside the project root"));
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
            if (!error_code && excluded_directory(item.path())) {
                iterator.disable_recursion_pending();
            }
            error_code.clear();
            continue;
        }
        error_code.clear();
        if (!item.is_regular_file(error_code) || error_code || !indexed_extension(item.path())) {
            error_code.clear();
            continue;
        }

        FileRecord record;
        record.path = fs::absolute(item.path(), error_code).lexically_normal();
        if (error_code) {
            error_code.clear();
            continue;
        }
        record.kind = source_extension(record.path) ? FileKind::source : FileKind::header;

        if (auto text = read_text(record.path)) {
            const std::string comment_free = strip_comments_preserve_literals(*text);
            record.local_includes = parse_local_includes(comment_free);
            record.defines_main = record.kind == FileKind::source && contains_main(comment_free);
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

    constexpr std::string_view source_extensions[]{".cpp", ".cc", ".cxx"};
    for (std::size_t file_index = 0; file_index < files.size(); ++file_index) {
        const auto& file = files[file_index];
        if (file.kind != FileKind::header) {
            continue;
        }
        for (const auto extension : source_extensions) {
            fs::path sibling = file.path;
            sibling.replace_extension(extension);
            const auto found = index_by_path.find(path_key(sibling));
            if (found != index_by_path.end()) {
                add_undirected_edge(adjacency, file_index, found->second);
            }
        }
    }

    std::vector<bool> visited(files.size(), false);
    std::deque<std::size_t> queue;
    queue.push_back(entry_it->second);
    visited[entry_it->second] = true;
    while (!queue.empty()) {
        const std::size_t current = queue.front();
        queue.pop_front();
        for (const std::size_t next : adjacency[current]) {
            if (!visited[next]) {
                visited[next] = true;
                queue.push_back(next);
            }
        }
    }

    for (std::size_t index = 0; index < files.size(); ++index) {
        if (!visited[index] || files[index].kind != FileKind::source) {
            continue;
        }
        if (files[index].path != entry && files[index].defines_main) {
            continue;
        }
        result.sources.push_back(files[index].path);
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
