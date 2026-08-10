#include "mqb/msvc/MsvcSourceDependenciesReader.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mqb::msvc {
namespace {

namespace fs = std::filesystem;

struct ParseFailure {
    std::size_t offset{};
    std::string message;
};

[[nodiscard]] fs::path path_from_utf8(const std::string_view value) {
    std::u8string bytes;
    bytes.assign(
        reinterpret_cast<const char8_t*>(value.data()),
        reinterpret_cast<const char8_t*>(value.data() + value.size()));
    return fs::path{bytes};
}

void append_utf8(std::string& output, const std::uint32_t code_point) {
    if (code_point <= 0x7fu) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7ffu) {
        output.push_back(static_cast<char>(0xc0u | (code_point >> 6u)));
        output.push_back(static_cast<char>(0x80u | (code_point & 0x3fu)));
    } else if (code_point <= 0xffffu) {
        output.push_back(static_cast<char>(0xe0u | (code_point >> 12u)));
        output.push_back(static_cast<char>(0x80u | ((code_point >> 6u) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | (code_point & 0x3fu)));
    } else {
        output.push_back(static_cast<char>(0xf0u | (code_point >> 18u)));
        output.push_back(static_cast<char>(0x80u | ((code_point >> 12u) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | ((code_point >> 6u) & 0x3fu)));
        output.push_back(static_cast<char>(0x80u | (code_point & 0x3fu)));
    }
}

class JsonCursor {
public:
    explicit JsonCursor(std::string_view input) : input_(input) {
        constexpr std::string_view utf8_bom{"\xef\xbb\xbf"};
        if (input_.starts_with(utf8_bom)) {
            position_ = utf8_bom.size();
        }
    }

    [[nodiscard]] std::size_t position() const noexcept {
        return position_;
    }

    void skip_whitespace() noexcept {
        while (position_ < input_.size()) {
            const unsigned char ch = static_cast<unsigned char>(input_[position_]);
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
                break;
            }
            ++position_;
        }
    }

    [[nodiscard]] bool consume(const char expected) noexcept {
        skip_whitespace();
        if (position_ >= input_.size() || input_[position_] != expected) {
            return false;
        }
        ++position_;
        return true;
    }

    [[nodiscard]] bool eof() noexcept {
        skip_whitespace();
        return position_ == input_.size();
    }

    [[nodiscard]] std::expected<std::string, ParseFailure> parse_string() {
        skip_whitespace();
        if (position_ >= input_.size() || input_[position_] != '"') {
            return fail("expected JSON string");
        }
        ++position_;

        std::string output;
        while (position_ < input_.size()) {
            const unsigned char ch = static_cast<unsigned char>(input_[position_++]);
            if (ch == '"') {
                return output;
            }
            if (ch < 0x20u) {
                return fail("unescaped control character in JSON string");
            }
            if (ch != '\\') {
                output.push_back(static_cast<char>(ch));
                continue;
            }

            if (position_ >= input_.size()) {
                return fail("unterminated JSON escape");
            }

            const char escaped = input_[position_++];
            switch (escaped) {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'u': {
                auto first = parse_hex_quad();
                if (!first) {
                    return std::unexpected(first.error());
                }

                std::uint32_t code_point = *first;
                if (code_point >= 0xd800u && code_point <= 0xdbffu) {
                    if (position_ + 2 > input_.size()
                        || input_[position_] != '\\'
                        || input_[position_ + 1] != 'u') {
                        return fail("high surrogate is not followed by a low surrogate");
                    }
                    position_ += 2;
                    auto second = parse_hex_quad();
                    if (!second) {
                        return std::unexpected(second.error());
                    }
                    if (*second < 0xdc00u || *second > 0xdfffu) {
                        return fail("invalid low surrogate in JSON string");
                    }
                    code_point = 0x10000u
                        + ((code_point - 0xd800u) << 10u)
                        + (*second - 0xdc00u);
                } else if (code_point >= 0xdc00u && code_point <= 0xdfffu) {
                    return fail("unexpected low surrogate in JSON string");
                }

                append_utf8(output, code_point);
                break;
            }
            default:
                return fail("invalid JSON escape sequence");
            }
        }

        return fail("unterminated JSON string");
    }

    [[nodiscard]] std::expected<void, ParseFailure> skip_value() {
        skip_whitespace();
        if (position_ >= input_.size()) {
            return std::unexpected(ParseFailure{position_, "expected JSON value"});
        }

        switch (input_[position_]) {
        case '"': {
            auto ignored = parse_string();
            if (!ignored) {
                return std::unexpected(ignored.error());
            }
            return {};
        }
        case '{':
            return skip_object();
        case '[':
            return skip_array();
        case 't':
            return consume_literal("true");
        case 'f':
            return consume_literal("false");
        case 'n':
            return consume_literal("null");
        default:
            if (input_[position_] == '-' || std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
                return skip_number();
            }
            return std::unexpected(ParseFailure{position_, "invalid JSON value"});
        }
    }

    [[nodiscard]] std::expected<std::vector<std::string>, ParseFailure> parse_string_array() {
        if (!consume('[')) {
            return fail_as<std::vector<std::string>>("expected JSON array");
        }

        std::vector<std::string> values;
        skip_whitespace();
        if (consume(']')) {
            return values;
        }

        for (;;) {
            auto value = parse_string();
            if (!value) {
                return std::unexpected(value.error());
            }
            values.push_back(std::move(*value));

            skip_whitespace();
            if (consume(']')) {
                return values;
            }
            if (!consume(',')) {
                return fail_as<std::vector<std::string>>("expected ',' or ']' in JSON array");
            }
        }
    }

private:
    template <typename T>
    [[nodiscard]] std::expected<T, ParseFailure> fail_as(std::string message) const {
        return std::unexpected(ParseFailure{position_, std::move(message)});
    }

    [[nodiscard]] std::expected<std::string, ParseFailure> fail(std::string message) const {
        return fail_as<std::string>(std::move(message));
    }

    [[nodiscard]] std::expected<std::uint32_t, ParseFailure> parse_hex_quad() {
        if (position_ + 4 > input_.size()) {
            return std::unexpected(ParseFailure{position_, "truncated JSON unicode escape"});
        }

        std::uint32_t value = 0;
        for (int index = 0; index < 4; ++index) {
            const char ch = input_[position_++];
            value <<= 4u;
            if (ch >= '0' && ch <= '9') {
                value |= static_cast<std::uint32_t>(ch - '0');
            } else if (ch >= 'a' && ch <= 'f') {
                value |= static_cast<std::uint32_t>(ch - 'a' + 10);
            } else if (ch >= 'A' && ch <= 'F') {
                value |= static_cast<std::uint32_t>(ch - 'A' + 10);
            } else {
                return std::unexpected(ParseFailure{position_ - 1, "invalid hexadecimal digit in JSON unicode escape"});
            }
        }
        return value;
    }

    [[nodiscard]] std::expected<void, ParseFailure> consume_literal(const std::string_view literal) {
        if (position_ + literal.size() > input_.size()
            || input_.substr(position_, literal.size()) != literal) {
            return std::unexpected(ParseFailure{position_, "invalid JSON literal"});
        }
        position_ += literal.size();
        return {};
    }

    [[nodiscard]] std::expected<void, ParseFailure> skip_number() {
        const std::size_t start = position_;
        if (position_ < input_.size() && input_[position_] == '-') {
            ++position_;
        }

        if (position_ >= input_.size()) {
            return std::unexpected(ParseFailure{start, "invalid JSON number"});
        }

        if (input_[position_] == '0') {
            ++position_;
        } else if (input_[position_] >= '1' && input_[position_] <= '9') {
            while (position_ < input_.size()
                && std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
                ++position_;
            }
        } else {
            return std::unexpected(ParseFailure{start, "invalid JSON number"});
        }

        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            const std::size_t fraction_start = position_;
            while (position_ < input_.size()
                && std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
                ++position_;
            }
            if (position_ == fraction_start) {
                return std::unexpected(ParseFailure{position_, "invalid JSON fraction"});
            }
        }

        if (position_ < input_.size()
            && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size()
                && (input_[position_] == '+' || input_[position_] == '-')) {
                ++position_;
            }
            const std::size_t exponent_start = position_;
            while (position_ < input_.size()
                && std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
                ++position_;
            }
            if (position_ == exponent_start) {
                return std::unexpected(ParseFailure{position_, "invalid JSON exponent"});
            }
        }
        return {};
    }

    [[nodiscard]] std::expected<void, ParseFailure> skip_object() {
        if (!consume('{')) {
            return std::unexpected(ParseFailure{position_, "expected JSON object"});
        }
        skip_whitespace();
        if (consume('}')) {
            return {};
        }

        for (;;) {
            auto key = parse_string();
            if (!key) {
                return std::unexpected(key.error());
            }
            if (!consume(':')) {
                return std::unexpected(ParseFailure{position_, "expected ':' after JSON object key"});
            }
            auto skipped = skip_value();
            if (!skipped) {
                return skipped;
            }
            if (consume('}')) {
                return {};
            }
            if (!consume(',')) {
                return std::unexpected(ParseFailure{position_, "expected ',' or '}' in JSON object"});
            }
        }
    }

    [[nodiscard]] std::expected<void, ParseFailure> skip_array() {
        if (!consume('[')) {
            return std::unexpected(ParseFailure{position_, "expected JSON array"});
        }
        if (consume(']')) {
            return {};
        }

        for (;;) {
            auto skipped = skip_value();
            if (!skipped) {
                return skipped;
            }
            if (consume(']')) {
                return {};
            }
            if (!consume(',')) {
                return std::unexpected(ParseFailure{position_, "expected ',' or ']' in JSON array"});
            }
        }
    }

    std::string_view input_;
    std::size_t position_{};
};

struct DataFields {
    std::optional<std::string> source;
    std::vector<std::string> includes;
};

[[nodiscard]] std::expected<DataFields, ParseFailure> parse_data_object(JsonCursor& cursor) {
    if (!cursor.consume('{')) {
        return std::unexpected(ParseFailure{cursor.position(), "Data must be a JSON object"});
    }

    DataFields fields;
    if (cursor.consume('}')) {
        return fields;
    }

    for (;;) {
        auto key = cursor.parse_string();
        if (!key) {
            return std::unexpected(key.error());
        }
        if (!cursor.consume(':')) {
            return std::unexpected(ParseFailure{cursor.position(), "expected ':' after Data key"});
        }

        if (*key == "Source") {
            auto source = cursor.parse_string();
            if (!source) {
                return std::unexpected(source.error());
            }
            fields.source = std::move(*source);
        } else if (*key == "Includes") {
            auto includes = cursor.parse_string_array();
            if (!includes) {
                return std::unexpected(includes.error());
            }
            fields.includes = std::move(*includes);
        } else {
            auto skipped = cursor.skip_value();
            if (!skipped) {
                return std::unexpected(skipped.error());
            }
        }

        if (cursor.consume('}')) {
            return fields;
        }
        if (!cursor.consume(',')) {
            return std::unexpected(ParseFailure{cursor.position(), "expected ',' or '}' in Data object"});
        }
    }
}

[[nodiscard]] std::expected<DataFields, SourceDependenciesError> parse_root(
    const std::string_view json) {
    JsonCursor cursor{json};
    if (!cursor.consume('{')) {
        return std::unexpected(SourceDependenciesError{
            .code = SourceDependenciesErrorCode::invalid_json,
            .offset = cursor.position(),
            .message = "sourceDependencies root must be a JSON object",
        });
    }

    std::optional<DataFields> data;
    if (!cursor.consume('}')) {
        for (;;) {
            auto key = cursor.parse_string();
            if (!key) {
                return std::unexpected(SourceDependenciesError{
                    .code = SourceDependenciesErrorCode::invalid_json,
                    .offset = key.error().offset,
                    .message = key.error().message,
                });
            }
            if (!cursor.consume(':')) {
                return std::unexpected(SourceDependenciesError{
                    .code = SourceDependenciesErrorCode::invalid_json,
                    .offset = cursor.position(),
                    .message = "expected ':' after root object key",
                });
            }

            if (*key == "Data") {
                auto parsed_data = parse_data_object(cursor);
                if (!parsed_data) {
                    return std::unexpected(SourceDependenciesError{
                        .code = SourceDependenciesErrorCode::invalid_schema,
                        .offset = parsed_data.error().offset,
                        .message = parsed_data.error().message,
                    });
                }
                data = std::move(*parsed_data);
            } else {
                auto skipped = cursor.skip_value();
                if (!skipped) {
                    return std::unexpected(SourceDependenciesError{
                        .code = SourceDependenciesErrorCode::invalid_json,
                        .offset = skipped.error().offset,
                        .message = skipped.error().message,
                    });
                }
            }

            if (cursor.consume('}')) {
                break;
            }
            if (!cursor.consume(',')) {
                return std::unexpected(SourceDependenciesError{
                    .code = SourceDependenciesErrorCode::invalid_json,
                    .offset = cursor.position(),
                    .message = "expected ',' or '}' in root object",
                });
            }
        }
    }

    if (!cursor.eof()) {
        return std::unexpected(SourceDependenciesError{
            .code = SourceDependenciesErrorCode::invalid_json,
            .offset = cursor.position(),
            .message = "unexpected data after root JSON object",
        });
    }
    if (!data) {
        return std::unexpected(SourceDependenciesError{
            .code = SourceDependenciesErrorCode::missing_data,
            .offset = cursor.position(),
            .message = "sourceDependencies JSON does not contain Data",
        });
    }
    if (!data->source || data->source->empty()) {
        return std::unexpected(SourceDependenciesError{
            .code = SourceDependenciesErrorCode::missing_source,
            .offset = cursor.position(),
            .message = "sourceDependencies Data.Source is missing or empty",
        });
    }
    return std::move(*data);
}

} // namespace

std::vector<std::filesystem::path> SourceDependencies::all_files() const {
    std::vector<std::filesystem::path> result;
    result.reserve(includes.size() + 1);
    result.push_back(source);
    for (const auto& include : includes) {
        if (std::find(result.begin(), result.end(), include) == result.end()) {
            result.push_back(include);
        }
    }
    return result;
}

std::expected<SourceDependencies, SourceDependenciesError>
MsvcSourceDependenciesReader::parse(const std::string_view json) {
    auto fields = parse_root(json);
    if (!fields) {
        return std::unexpected(fields.error());
    }

    SourceDependencies dependencies;
    dependencies.source = path_from_utf8(*fields->source).lexically_normal();
    dependencies.includes.reserve(fields->includes.size());
    for (const auto& include : fields->includes) {
        dependencies.includes.push_back(path_from_utf8(include).lexically_normal());
    }
    return dependencies;
}

std::expected<SourceDependencies, SourceDependenciesError>
MsvcSourceDependenciesReader::read(const fs::path& file) {
    std::ifstream stream{file, std::ios::binary};
    if (!stream) {
        return std::unexpected(SourceDependenciesError{
            .code = SourceDependenciesErrorCode::file_open_failed,
            .file = file,
            .message = "failed to open sourceDependencies JSON",
        });
    }

    std::string bytes{
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{}};
    if (stream.bad()) {
        return std::unexpected(SourceDependenciesError{
            .code = SourceDependenciesErrorCode::file_read_failed,
            .file = file,
            .message = "failed while reading sourceDependencies JSON",
        });
    }

    auto parsed = parse(bytes);
    if (!parsed) {
        auto error = parsed.error();
        error.file = file;
        return std::unexpected(std::move(error));
    }
    return parsed;
}

} // namespace mqb::msvc
