#include "mqb/discovery/ModuleSyntax.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mqb::discovery {
namespace {

enum class TokenKind {
    identifier,
    colon,
    dot,
    semicolon,
    less,
    string_literal,
    other,
};

struct Token {
    TokenKind kind{TokenKind::other};
    std::string text;
};

[[nodiscard]] bool identifier_start(const char ch) {
    const unsigned char value = static_cast<unsigned char>(ch);
    return std::isalpha(value) != 0 || ch == '_';
}

[[nodiscard]] bool identifier_continue(const char ch) {
    const unsigned char value = static_cast<unsigned char>(ch);
    return std::isalnum(value) != 0 || ch == '_';
}

[[nodiscard]] bool starts_with_at(
    const std::string_view text,
    const std::size_t offset,
    const std::string_view prefix) {
    return offset <= text.size() && text.substr(offset, prefix.size()) == prefix;
}

[[nodiscard]] std::optional<std::size_t> literal_prefix_length(
    const std::string_view text,
    const std::size_t offset,
    bool& raw,
    char& quote) {
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
            raw = prefix.raw;
            quote = prefix.quote;
            return prefix.text.size();
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::size_t skip_literal(
    const std::string_view text,
    const std::size_t offset,
    const std::size_t prefix_length,
    const bool raw,
    const char quote) {
    if (raw) {
        // prefix_length includes the opening quote. The raw delimiter follows
        // that quote and ends at the first '('. Invalid/incomplete literals are
        // conservatively consumed to EOF so their contents cannot create edges.
        const std::size_t delimiter_begin = offset + prefix_length;
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

    std::size_t cursor = offset + prefix_length;
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
        if (ch == quote) {
            return cursor;
        }
    }
    return text.size();
}

[[nodiscard]] std::vector<Token> tokenize(const std::string_view text) {
    std::vector<Token> tokens;
    bool line_has_code = false;

    for (std::size_t index = 0; index < text.size();) {
        const char ch = text[index];
        const char next = index + 1 < text.size() ? text[index + 1] : '\0';

        if (ch == '\n' || ch == '\r') {
            line_has_code = false;
            ++index;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            ++index;
            continue;
        }

        // Ignore preprocessor directive lines, including simple backslash
        // continuations. Module declarations/import-declarations are not
        // preprocessor directives, so this avoids macro-text false positives.
        if (!line_has_code && ch == '#') {
            while (index < text.size()) {
                const std::size_t newline = text.find('\n', index);
                if (newline == std::string_view::npos) {
                    index = text.size();
                    break;
                }
                std::size_t before = newline;
                while (before > index
                       && (text[before - 1] == ' ' || text[before - 1] == '\t'
                           || text[before - 1] == '\r')) {
                    --before;
                }
                const bool continued = before > index && text[before - 1] == '\\';
                index = newline + 1;
                if (!continued) break;
            }
            line_has_code = false;
            continue;
        }

        if (ch == '/' && next == '/') {
            const std::size_t newline = text.find('\n', index + 2);
            index = newline == std::string_view::npos ? text.size() : newline;
            continue;
        }
        if (ch == '/' && next == '*') {
            const std::size_t close = text.find("*/", index + 2);
            const std::size_t comment_end = close == std::string_view::npos
                ? text.size()
                : close + 2;
            const std::string_view comment = text.substr(index, comment_end - index);
            if (comment.find('\n') != std::string_view::npos
                || comment.find('\r') != std::string_view::npos) {
                // A block comment is preprocessing whitespace. If it crosses a
                // physical line boundary, code before the comment must not make
                // a following `#` look like mid-line code on the new line.
                line_has_code = false;
            }
            index = comment_end;
            continue;
        }

        bool raw = false;
        char quote = '\0';
        if (auto prefix = literal_prefix_length(text, index, raw, quote)) {
            tokens.push_back(Token{.kind = TokenKind::string_literal});
            index = skip_literal(text, index, *prefix, raw, quote);
            line_has_code = true;
            continue;
        }

        if (identifier_start(ch)) {
            const std::size_t begin = index++;
            while (index < text.size() && identifier_continue(text[index])) ++index;
            tokens.push_back(Token{
                .kind = TokenKind::identifier,
                .text = std::string{text.substr(begin, index - begin)},
            });
            line_has_code = true;
            continue;
        }

        TokenKind kind = TokenKind::other;
        switch (ch) {
        case ':': kind = TokenKind::colon; break;
        case '.': kind = TokenKind::dot; break;
        case ';': kind = TokenKind::semicolon; break;
        case '<': kind = TokenKind::less; break;
        default: break;
        }
        tokens.push_back(Token{.kind = kind, .text = std::string(1, ch)});
        line_has_code = true;
        ++index;
    }
    return tokens;
}

struct ParsedName {
    std::string name;
    std::size_t semicolon_index{};
    bool relative_partition{false};
};

[[nodiscard]] std::optional<ParsedName> parse_module_name(
    const std::vector<Token>& tokens,
    std::size_t index,
    const bool allow_relative) {
    ParsedName parsed;
    if (index < tokens.size() && tokens[index].kind == TokenKind::colon) {
        if (!allow_relative) return std::nullopt;
        parsed.relative_partition = true;
        parsed.name.push_back(':');
        ++index;
    }

    if (index >= tokens.size() || tokens[index].kind != TokenKind::identifier) {
        return std::nullopt;
    }
    parsed.name += tokens[index++].text;

    bool saw_partition = parsed.relative_partition;
    while (index < tokens.size()) {
        if (tokens[index].kind == TokenKind::dot) {
            if (index + 1 >= tokens.size()
                || tokens[index + 1].kind != TokenKind::identifier) {
                return std::nullopt;
            }
            parsed.name.push_back('.');
            parsed.name += tokens[index + 1].text;
            index += 2;
            continue;
        }
        if (tokens[index].kind == TokenKind::colon && !saw_partition) {
            if (index + 1 >= tokens.size()
                || tokens[index + 1].kind != TokenKind::identifier) {
                return std::nullopt;
            }
            saw_partition = true;
            parsed.name.push_back(':');
            parsed.name += tokens[index + 1].text;
            index += 2;
            continue;
        }
        break;
    }

    if (index >= tokens.size() || tokens[index].kind != TokenKind::semicolon) {
        return std::nullopt;
    }
    parsed.semicolon_index = index;
    return parsed;
}

[[nodiscard]] std::string primary_module_name(const std::string& declared) {
    const std::size_t partition = declared.find(':');
    return partition == std::string::npos ? declared : declared.substr(0, partition);
}

void append_unique(std::vector<std::string>& values, std::string value) {
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(std::move(value));
    }
}

} // namespace

NamedModuleSyntax ModuleSyntaxParser::parse(const std::string_view source_text) {
    const std::vector<Token> tokens = tokenize(source_text);
    NamedModuleSyntax syntax;

    // First determine the named module declaration so relative partition imports
    // can be normalized even though import extraction is a separate pass.
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        std::size_t module_index = index;
        if (tokens[index].kind == TokenKind::identifier
            && tokens[index].text == "export") {
            if (index + 1 >= tokens.size()
                || tokens[index + 1].kind != TokenKind::identifier
                || tokens[index + 1].text != "module") {
                continue;
            }
            module_index = index + 1;
        } else if (tokens[index].kind != TokenKind::identifier
                   || tokens[index].text != "module") {
            continue;
        }

        const std::size_t name_index = module_index + 1;
        if (name_index >= tokens.size()
            || tokens[name_index].kind == TokenKind::semicolon
            || tokens[name_index].kind == TokenKind::colon) {
            // global module fragment `module;` and private fragment
            // `module : private;` are not named providers.
            continue;
        }
        if (auto parsed = parse_module_name(tokens, name_index, false)) {
            syntax.declared_module = std::move(parsed->name);
            break;
        }
    }

    for (std::size_t index = 0; index < tokens.size(); ++index) {
        std::size_t import_index = index;
        if (tokens[index].kind == TokenKind::identifier
            && tokens[index].text == "export") {
            if (index + 1 >= tokens.size()
                || tokens[index + 1].kind != TokenKind::identifier
                || tokens[index + 1].text != "import") {
                continue;
            }
            import_index = index + 1;
        } else if (tokens[index].kind != TokenKind::identifier
                   || tokens[index].text != "import") {
            continue;
        }

        const std::size_t name_index = import_index + 1;
        if (name_index >= tokens.size()) continue;
        if (tokens[name_index].kind == TokenKind::less
            || tokens[name_index].kind == TokenKind::string_literal) {
            // Header units are intentionally not source-discovery named-module
            // edges. The real module pipeline continues to fail them closed.
            continue;
        }

        auto parsed = parse_module_name(tokens, name_index, true);
        if (!parsed) continue;

        std::string name = std::move(parsed->name);
        if (parsed->relative_partition) {
            if (!syntax.declared_module) continue;
            name = primary_module_name(*syntax.declared_module) + name;
        }
        append_unique(syntax.imported_modules, std::move(name));
    }

    return syntax;
}

} // namespace mqb::discovery
