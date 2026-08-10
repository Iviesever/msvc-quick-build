#include "mqb/json/Json.hpp"

#include <cctype>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace mqb::json {
namespace {

class Parser {
public:
    explicit Parser(const std::string_view text) : text_(text) {}

    [[nodiscard]] std::expected<Value, Error> parse_document() {
        skip_ws();
        auto value = parse_value();
        if (!value) return std::unexpected(value.error());
        skip_ws();
        if (!eof()) {
            return std::unexpected(error("unexpected characters after JSON value"));
        }
        return value;
    }

private:
    [[nodiscard]] bool eof() const noexcept { return pos_ >= text_.size(); }
    [[nodiscard]] char peek() const noexcept { return eof() ? '\0' : text_[pos_]; }

    char take() {
        const char ch = text_[pos_++];
        if (ch == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        return ch;
    }

    void skip_ws() {
        while (!eof()) {
            const char ch = peek();
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') break;
            take();
        }
    }

    [[nodiscard]] Error error(std::string message) const {
        return Error{
            .line = line_,
            .column = column_,
            .message = std::move(message),
        };
    }

    [[nodiscard]] std::expected<Value, Error> parse_value() {
        if (eof()) {
            return std::unexpected(error("unexpected end of JSON input"));
        }

        const std::size_t start_line = line_;
        const std::size_t start_column = column_;
        switch (peek()) {
        case '{':
            return parse_object(start_line, start_column);
        case '[':
            return parse_array(start_line, start_column);
        case '"': {
            auto text = parse_string();
            if (!text) return std::unexpected(text.error());
            Value value;
            value.kind = Kind::string;
            value.line = start_line;
            value.column = start_column;
            value.scalar = std::move(*text);
            return value;
        }
        case 't':
            return parse_literal("true", Kind::boolean, true, start_line, start_column);
        case 'f':
            return parse_literal("false", Kind::boolean, false, start_line, start_column);
        case 'n':
            return parse_literal("null", Kind::null_value, false, start_line, start_column);
        default:
            if (peek() == '-' || std::isdigit(static_cast<unsigned char>(peek()))) {
                return parse_number(start_line, start_column);
            }
            return std::unexpected(error("unexpected token in JSON input"));
        }
    }

    [[nodiscard]] std::expected<Value, Error> parse_literal(
        const std::string_view literal,
        const Kind kind,
        const bool boolean,
        const std::size_t start_line,
        const std::size_t start_column) {
        for (const char expected : literal) {
            if (eof() || take() != expected) {
                return std::unexpected(error("invalid JSON literal"));
            }
        }

        Value value;
        value.kind = kind;
        value.line = start_line;
        value.column = start_column;
        value.boolean = boolean;
        return value;
    }

    [[nodiscard]] std::expected<Value, Error> parse_object(
        const std::size_t start_line,
        const std::size_t start_column) {
        take();
        Value value;
        value.kind = Kind::object;
        value.line = start_line;
        value.column = start_column;
        skip_ws();
        if (!eof() && peek() == '}') {
            take();
            return value;
        }

        while (true) {
            skip_ws();
            if (eof() || peek() != '"') {
                return std::unexpected(error("expected string key in JSON object"));
            }
            const std::size_t key_line = line_;
            const std::size_t key_column = column_;
            auto key = parse_string();
            if (!key) return std::unexpected(key.error());

            skip_ws();
            if (eof() || take() != ':') {
                return std::unexpected(error("expected ':' after JSON object key"));
            }
            skip_ws();

            auto member = parse_value();
            if (!member) return std::unexpected(member.error());
            if (value.object.contains(*key)) {
                return std::unexpected(Error{
                    .line = key_line,
                    .column = key_column,
                    .message = "duplicate JSON object key '" + *key + "'",
                });
            }
            value.object.emplace(std::move(*key), std::move(*member));

            skip_ws();
            if (eof()) {
                return std::unexpected(error("unterminated JSON object"));
            }
            const char delimiter = take();
            if (delimiter == '}') break;
            if (delimiter != ',') {
                return std::unexpected(error("expected ',' or '}' in JSON object"));
            }
        }
        return value;
    }

    [[nodiscard]] std::expected<Value, Error> parse_array(
        const std::size_t start_line,
        const std::size_t start_column) {
        take();
        Value value;
        value.kind = Kind::array;
        value.line = start_line;
        value.column = start_column;
        skip_ws();
        if (!eof() && peek() == ']') {
            take();
            return value;
        }

        while (true) {
            skip_ws();
            auto element = parse_value();
            if (!element) return std::unexpected(element.error());
            value.array.push_back(std::move(*element));

            skip_ws();
            if (eof()) {
                return std::unexpected(error("unterminated JSON array"));
            }
            const char delimiter = take();
            if (delimiter == ']') break;
            if (delimiter != ',') {
                return std::unexpected(error("expected ',' or ']' in JSON array"));
            }
        }
        return value;
    }

    [[nodiscard]] static int hex(const char ch) noexcept {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
        if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
        return -1;
    }

    [[nodiscard]] std::expected<std::uint32_t, Error> parse_hex_quad() {
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            if (eof()) {
                return std::unexpected(error("incomplete Unicode escape"));
            }
            const int digit = hex(take());
            if (digit < 0) {
                return std::unexpected(error("invalid Unicode escape"));
            }
            value = (value << 4u) | static_cast<std::uint32_t>(digit);
        }
        return value;
    }

    static void append_utf8(std::string& out, const std::uint32_t cp) {
        if (cp <= 0x7fu) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7ffu) {
            out.push_back(static_cast<char>(0xc0u | (cp >> 6u)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3fu)));
        } else if (cp <= 0xffffu) {
            out.push_back(static_cast<char>(0xe0u | (cp >> 12u)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3fu)));
        } else {
            out.push_back(static_cast<char>(0xf0u | (cp >> 18u)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 12u) & 0x3fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3fu)));
        }
    }

    [[nodiscard]] std::expected<std::string, Error> parse_string() {
        if (eof() || take() != '"') {
            return std::unexpected(error("expected JSON string"));
        }

        std::string out;
        while (!eof()) {
            const unsigned char raw = static_cast<unsigned char>(take());
            if (raw == '"') return out;
            if (raw < 0x20u) {
                return std::unexpected(error("unescaped control character in JSON string"));
            }
            if (raw != '\\') {
                out.push_back(static_cast<char>(raw));
                continue;
            }

            if (eof()) {
                return std::unexpected(error("incomplete JSON string escape"));
            }
            switch (take()) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                auto first = parse_hex_quad();
                if (!first) return std::unexpected(first.error());
                std::uint32_t cp = *first;
                if (cp >= 0xd800u && cp <= 0xdbffu) {
                    if (eof() || take() != '\\' || eof() || take() != 'u') {
                        return std::unexpected(error(
                            "high surrogate must be followed by low surrogate"));
                    }
                    auto second = parse_hex_quad();
                    if (!second) return std::unexpected(second.error());
                    if (*second < 0xdc00u || *second > 0xdfffu) {
                        return std::unexpected(error(
                            "invalid low surrogate in Unicode escape"));
                    }
                    cp = 0x10000u
                        + ((cp - 0xd800u) << 10u)
                        + (*second - 0xdc00u);
                } else if (cp >= 0xdc00u && cp <= 0xdfffu) {
                    return std::unexpected(error(
                        "unexpected low surrogate in Unicode escape"));
                }
                append_utf8(out, cp);
                break;
            }
            default:
                return std::unexpected(error("invalid JSON string escape"));
            }
        }
        return std::unexpected(error("unterminated JSON string"));
    }

    [[nodiscard]] std::expected<Value, Error> parse_number(
        const std::size_t start_line,
        const std::size_t start_column) {
        const std::size_t begin = pos_;
        if (peek() == '-') take();
        if (eof()) {
            return std::unexpected(error("incomplete JSON number"));
        }

        if (peek() == '0') {
            take();
            if (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) {
                return std::unexpected(error("leading zero in JSON number"));
            }
        } else if (peek() >= '1' && peek() <= '9') {
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) take();
        } else {
            return std::unexpected(error("invalid JSON number"));
        }

        if (!eof() && peek() == '.') {
            take();
            if (eof() || !std::isdigit(static_cast<unsigned char>(peek()))) {
                return std::unexpected(error("JSON fraction requires digits"));
            }
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) take();
        }

        if (!eof() && (peek() == 'e' || peek() == 'E')) {
            take();
            if (!eof() && (peek() == '+' || peek() == '-')) take();
            if (eof() || !std::isdigit(static_cast<unsigned char>(peek()))) {
                return std::unexpected(error("JSON exponent requires digits"));
            }
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) take();
        }

        Value value;
        value.kind = Kind::number;
        value.line = start_line;
        value.column = start_column;
        value.scalar = std::string{text_.substr(begin, pos_ - begin)};
        return value;
    }

    std::string_view text_;
    std::size_t pos_{};
    std::size_t line_{1};
    std::size_t column_{1};
};

} // namespace

std::expected<Value, Error> parse(const std::string_view text) {
    return Parser{text}.parse_document();
}

} // namespace mqb::json
