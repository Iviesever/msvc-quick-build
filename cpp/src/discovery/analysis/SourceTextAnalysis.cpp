#include "SourceTextAnalysis.hpp"

#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace mqb::discovery::detail {
namespace {

struct LiteralPrefix {
    std::size_t length{};
    bool raw{false};
    char quote{'\0'};
};

[[nodiscard]] bool starts_with_at(
    const std::string_view text,
    const std::size_t offset,
    const std::string_view prefix) {
    return offset <= text.size() && text.substr(offset, prefix.size()) == prefix;
}

[[nodiscard]] std::optional<LiteralPrefix> literal_prefix(
    const std::string_view text,
    const std::size_t offset) {
    struct Prefix {
        std::string_view text;
        bool raw;
        char quote;
    };
    constexpr Prefix prefixes[]{
        {"u8R\"", true, '"'}, {"uR\"", true, '"'}, {"UR\"", true, '"'},
        {"LR\"", true, '"'}, {"R\"", true, '"'},
        {"u8\"", false, '"'}, {"u\"", false, '"'}, {"U\"", false, '"'},
        {"L\"", false, '"'}, {"\"", false, '"'},
        {"u8'", false, '\''}, {"u'", false, '\''}, {"U'", false, '\''},
        {"L'", false, '\''}, {"'", false, '\''},
    };
    for (const auto& prefix : prefixes) {
        if (starts_with_at(text, offset, prefix.text)) {
            return LiteralPrefix{
                .length = prefix.text.size(),
                .raw = prefix.raw,
                .quote = prefix.quote,
            };
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::size_t literal_end(
    const std::string_view text,
    const std::size_t offset,
    const LiteralPrefix prefix) {
    if (prefix.raw) {
        const std::size_t delimiter_begin = offset + prefix.length;
        const std::size_t open = text.find('(', delimiter_begin);
        if (open == std::string_view::npos || open - delimiter_begin > 16) {
            return text.size();
        }
        const std::string delimiter{text.substr(delimiter_begin, open - delimiter_begin)};
        const std::string terminator = ")" + delimiter + "\"";
        const std::size_t close = text.find(terminator, open + 1);
        return close == std::string_view::npos
            ? text.size()
            : close + terminator.size();
    }

    std::size_t cursor = offset + prefix.length;
    bool escaped = false;
    while (cursor < text.size()) {
        const char ch = text[cursor++];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == prefix.quote) {
            return cursor;
        }
    }
    return text.size();
}

void mask_span(
    std::string& code,
    const std::size_t begin,
    const std::size_t end) {
    for (std::size_t index = begin; index < end && index < code.size(); ++index) {
        if (code[index] != '\n' && code[index] != '\r') {
            code[index] = ' ';
        }
    }
}

[[nodiscard]] bool identifier_continue(const char ch) {
    const auto value = static_cast<unsigned char>(ch);
    return std::isalnum(value) != 0 || ch == '_';
}

void collect_include(
    const std::string_view source,
    const std::size_t hash,
    std::vector<std::string>& quoted_includes,
    std::vector<std::string>& angle_includes) {
    std::size_t cursor = hash + 1;
    while (cursor < source.size()
           && source[cursor] != '\n'
           && source[cursor] != '\r'
           && std::isspace(static_cast<unsigned char>(source[cursor])) != 0) {
        ++cursor;
    }

    constexpr std::string_view keyword = "include";
    if (source.substr(cursor, keyword.size()) != keyword) {
        return;
    }
    cursor += keyword.size();
    if (cursor < source.size() && identifier_continue(source[cursor])) {
        return;
    }
    while (cursor < source.size()
           && source[cursor] != '\n'
           && source[cursor] != '\r'
           && std::isspace(static_cast<unsigned char>(source[cursor])) != 0) {
        ++cursor;
    }
    if (cursor >= source.size()) {
        return;
    }

    const char opener = source[cursor];
    const char closer = opener == '"' ? '"' : opener == '<' ? '>' : '\0';
    if (closer == '\0') {
        // Macro-expanded include operands are intentionally outside the lexical
        // discovery model; P1689/compiler dependency evidence remains the
        // authority once the TU is selected.
        return;
    }

    const std::size_t begin = ++cursor;
    while (cursor < source.size()
           && source[cursor] != closer
           && source[cursor] != '\n'
           && source[cursor] != '\r') {
        ++cursor;
    }
    if (cursor <= begin || cursor >= source.size() || source[cursor] != closer) {
        return;
    }

    auto& destination = opener == '"' ? quoted_includes : angle_includes;
    destination.emplace_back(source.substr(begin, cursor - begin));
}

struct LexicalScan {
    std::string code;
    std::vector<std::string> quoted_includes;
    std::vector<std::string> angle_includes;
};

[[nodiscard]] std::size_t directive_end(
    const std::string_view source,
    std::string& code,
    const std::size_t hash) {
    std::size_t line_begin = hash;
    while (line_begin < source.size()) {
        const std::size_t newline = source.find('\n', line_begin);
        if (newline == std::string_view::npos) {
            mask_span(code, line_begin, source.size());
            return source.size();
        }

        std::size_t before = newline;
        while (before > line_begin
               && (source[before - 1] == ' '
                   || source[before - 1] == '\t'
                   || source[before - 1] == '\r')) {
            --before;
        }
        const bool continued = before > line_begin && source[before - 1] == '\\';
        mask_span(code, line_begin, newline);
        if (!continued) {
            return newline;
        }
        line_begin = newline + 1;
    }
    return source.size();
}

[[nodiscard]] LexicalScan scan_source(const std::string_view source) {
    LexicalScan scan{
        .code = std::string{source},
        .quoted_includes = {},
        .angle_includes = {},
    };
    bool line_has_code = false;

    for (std::size_t index = 0; index < source.size();) {
        const char ch = source[index];
        const char next = index + 1 < source.size() ? source[index + 1] : '\0';

        if (ch == '\n' || ch == '\r') {
            line_has_code = false;
            ++index;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            ++index;
            continue;
        }

        if (!line_has_code && ch == '#') {
            collect_include(
                source,
                index,
                scan.quoted_includes,
                scan.angle_includes);
            index = directive_end(source, scan.code, index);
            line_has_code = false;
            continue;
        }

        if (ch == '/' && next == '/') {
            const std::size_t newline = source.find('\n', index + 2);
            const std::size_t end = newline == std::string_view::npos ? source.size() : newline;
            mask_span(scan.code, index, end);
            index = end;
            continue;
        }
        if (ch == '/' && next == '*') {
            const std::size_t close = source.find("*/", index + 2);
            const std::size_t end = close == std::string_view::npos ? source.size() : close + 2;
            const bool crossed_line = source.substr(index, end - index).find_first_of("\r\n")
                != std::string_view::npos;
            mask_span(scan.code, index, end);
            index = end;
            if (crossed_line) {
                line_has_code = false;
            }
            continue;
        }

        if (const auto prefix = literal_prefix(source, index)) {
            const std::size_t end = literal_end(source, index, *prefix);
            mask_span(scan.code, index, end);
            index = end;
            line_has_code = true;
            continue;
        }

        line_has_code = true;
        ++index;
    }
    return scan;
}

[[nodiscard]] bool contains_main(const std::string_view code) {
    std::size_t cursor = 0;
    while ((cursor = code.find("main", cursor)) != std::string_view::npos) {
        const bool left_boundary = cursor == 0 || !identifier_continue(code[cursor - 1]);
        const std::size_t after_name = cursor + 4;
        const bool right_boundary = after_name >= code.size()
            || !identifier_continue(code[after_name]);
        if (left_boundary && right_boundary) {
            std::size_t next = after_name;
            while (next < code.size()
                   && std::isspace(static_cast<unsigned char>(code[next])) != 0) {
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

} // namespace

SourceTextAnalysis analyze_source_text(
    const std::string_view source_text,
    const bool parse_module_syntax) {
    LexicalScan scan = scan_source(source_text);
    return SourceTextAnalysis{
        .quoted_includes = std::move(scan.quoted_includes),
        .angle_includes = std::move(scan.angle_includes),
        .module_syntax = parse_module_syntax
            ? ModuleSyntaxParser::parse(source_text)
            : NamedModuleSyntax{},
        .defines_main = contains_main(scan.code),
    };
}

} // namespace mqb::discovery::detail
