#include "mqb/config/ProjectConfig.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace mqb::config {
namespace {

namespace fs = std::filesystem;

struct JsonValue {
    enum class Kind { null_value, boolean, number, string, array, object };
    Kind kind{Kind::null_value};
    std::size_t line{1};
    std::size_t column{1};
    bool boolean{};
    std::string scalar;
    std::vector<JsonValue> array;
    std::map<std::string, JsonValue, std::less<>> object;
};
using JsonObject = decltype(JsonValue{}.object);

[[nodiscard]] Error make_error(
    const ErrorCode code,
    const fs::path& path,
    const std::size_t line,
    const std::size_t column,
    std::string message) {
    return Error{code, path, line, column, std::move(message)};
}

class JsonParser {
public:
    JsonParser(std::string_view text, fs::path path)
        : text_(text), path_(std::move(path)) {}

    [[nodiscard]] std::expected<JsonValue, Error> parse() {
        skip_ws();
        auto value = parse_value();
        if (!value) return std::unexpected(value.error());
        skip_ws();
        if (!eof()) return std::unexpected(error("unexpected characters after JSON value"));
        return value;
    }

private:
    [[nodiscard]] bool eof() const noexcept { return pos_ >= text_.size(); }
    [[nodiscard]] char peek() const noexcept { return eof() ? '\0' : text_[pos_]; }
    char take() {
        const char ch = text_[pos_++];
        if (ch == '\n') { ++line_; column_ = 1; }
        else { ++column_; }
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
        return make_error(ErrorCode::parse_error, path_, line_, column_, std::move(message));
    }

    [[nodiscard]] std::expected<JsonValue, Error> parse_value() {
        if (eof()) return std::unexpected(error("unexpected end of JSON input"));
        const auto start_line = line_;
        const auto start_column = column_;
        switch (peek()) {
        case '{': return parse_object(start_line, start_column);
        case '[': return parse_array(start_line, start_column);
        case '"': {
            auto text = parse_string();
            if (!text) return std::unexpected(text.error());
            JsonValue value;
            value.kind = JsonValue::Kind::string;
            value.line = start_line;
            value.column = start_column;
            value.scalar = std::move(*text);
            return value;
        }
        case 't': return parse_literal("true", JsonValue::Kind::boolean, true, start_line, start_column);
        case 'f': return parse_literal("false", JsonValue::Kind::boolean, false, start_line, start_column);
        case 'n': return parse_literal("null", JsonValue::Kind::null_value, false, start_line, start_column);
        default:
            if (peek() == '-' || std::isdigit(static_cast<unsigned char>(peek()))) {
                return parse_number(start_line, start_column);
            }
            return std::unexpected(error("unexpected token in JSON input"));
        }
    }

    [[nodiscard]] std::expected<JsonValue, Error> parse_literal(
        std::string_view literal,
        JsonValue::Kind kind,
        bool boolean,
        std::size_t start_line,
        std::size_t start_column) {
        for (const char expected : literal) {
            if (eof() || take() != expected) return std::unexpected(error("invalid JSON literal"));
        }
        JsonValue value;
        value.kind = kind;
        value.line = start_line;
        value.column = start_column;
        value.boolean = boolean;
        return value;
    }

    [[nodiscard]] std::expected<JsonValue, Error> parse_object(
        std::size_t start_line,
        std::size_t start_column) {
        take();
        JsonValue value;
        value.kind = JsonValue::Kind::object;
        value.line = start_line;
        value.column = start_column;
        skip_ws();
        if (!eof() && peek() == '}') { take(); return value; }
        while (true) {
            skip_ws();
            if (eof() || peek() != '"') return std::unexpected(error("expected string key in JSON object"));
            const auto key_line = line_;
            const auto key_column = column_;
            auto key = parse_string();
            if (!key) return std::unexpected(key.error());
            skip_ws();
            if (eof() || take() != ':') return std::unexpected(error("expected ':' after JSON object key"));
            skip_ws();
            auto member = parse_value();
            if (!member) return std::unexpected(member.error());
            if (value.object.contains(*key)) {
                return std::unexpected(make_error(
                    ErrorCode::parse_error,
                    path_,
                    key_line,
                    key_column,
                    "duplicate JSON object key '" + *key + "'"));
            }
            value.object.emplace(std::move(*key), std::move(*member));
            skip_ws();
            if (eof()) return std::unexpected(error("unterminated JSON object"));
            const char delimiter = take();
            if (delimiter == '}') break;
            if (delimiter != ',') return std::unexpected(error("expected ',' or '}' in JSON object"));
        }
        return value;
    }

    [[nodiscard]] std::expected<JsonValue, Error> parse_array(
        std::size_t start_line,
        std::size_t start_column) {
        take();
        JsonValue value;
        value.kind = JsonValue::Kind::array;
        value.line = start_line;
        value.column = start_column;
        skip_ws();
        if (!eof() && peek() == ']') { take(); return value; }
        while (true) {
            skip_ws();
            auto element = parse_value();
            if (!element) return std::unexpected(element.error());
            value.array.push_back(std::move(*element));
            skip_ws();
            if (eof()) return std::unexpected(error("unterminated JSON array"));
            const char delimiter = take();
            if (delimiter == ']') break;
            if (delimiter != ',') return std::unexpected(error("expected ',' or ']' in JSON array"));
        }
        return value;
    }

    [[nodiscard]] static int hex(char ch) noexcept {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
        if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
        return -1;
    }
    [[nodiscard]] std::expected<std::uint32_t, Error> parse_hex_quad() {
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            if (eof()) return std::unexpected(error("incomplete Unicode escape"));
            const int digit = hex(take());
            if (digit < 0) return std::unexpected(error("invalid Unicode escape"));
            value = (value << 4u) | static_cast<std::uint32_t>(digit);
        }
        return value;
    }
    static void append_utf8(std::string& out, std::uint32_t cp) {
        if (cp <= 0x7fu) out.push_back(static_cast<char>(cp));
        else if (cp <= 0x7ffu) {
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
        if (eof() || take() != '"') return std::unexpected(error("expected JSON string"));
        std::string out;
        while (!eof()) {
            const unsigned char raw = static_cast<unsigned char>(take());
            if (raw == '"') return out;
            if (raw < 0x20u) return std::unexpected(error("unescaped control character in JSON string"));
            if (raw != '\\') { out.push_back(static_cast<char>(raw)); continue; }
            if (eof()) return std::unexpected(error("incomplete JSON string escape"));
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
                        return std::unexpected(error("high surrogate must be followed by low surrogate"));
                    }
                    auto second = parse_hex_quad();
                    if (!second) return std::unexpected(second.error());
                    if (*second < 0xdc00u || *second > 0xdfffu) {
                        return std::unexpected(error("invalid low surrogate in Unicode escape"));
                    }
                    cp = 0x10000u + ((cp - 0xd800u) << 10u) + (*second - 0xdc00u);
                } else if (cp >= 0xdc00u && cp <= 0xdfffu) {
                    return std::unexpected(error("unexpected low surrogate in Unicode escape"));
                }
                append_utf8(out, cp);
                break;
            }
            default: return std::unexpected(error("invalid JSON string escape"));
            }
        }
        return std::unexpected(error("unterminated JSON string"));
    }

    [[nodiscard]] std::expected<JsonValue, Error> parse_number(
        std::size_t start_line,
        std::size_t start_column) {
        const std::size_t begin = pos_;
        if (peek() == '-') take();
        if (eof()) return std::unexpected(error("incomplete JSON number"));
        if (peek() == '0') {
            take();
            if (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) {
                return std::unexpected(error("leading zero in JSON number"));
            }
        } else if (peek() >= '1' && peek() <= '9') {
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) take();
        } else return std::unexpected(error("invalid JSON number"));
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
        JsonValue value;
        value.kind = JsonValue::Kind::number;
        value.line = start_line;
        value.column = start_column;
        value.scalar = std::string{text_.substr(begin, pos_ - begin)};
        return value;
    }

    std::string_view text_;
    fs::path path_;
    std::size_t pos_{};
    std::size_t line_{1};
    std::size_t column_{1};
};

[[nodiscard]] Error schema_error(const fs::path& path, const JsonValue& value, std::string message) {
    return make_error(ErrorCode::schema_error, path, value.line, value.column, std::move(message));
}

[[nodiscard]] std::expected<std::string, Error> read_file(const fs::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) return std::unexpected(make_error(ErrorCode::io_error, path, 1, 1, "failed to open project config"));
    std::string text{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    if (!stream.eof() && stream.fail()) {
        return std::unexpected(make_error(ErrorCode::io_error, path, 1, 1, "failed while reading project config"));
    }
    return text;
}

[[nodiscard]] std::expected<const JsonObject*, Error>
require_object(const fs::path& path, const JsonValue& value, std::string_view name) {
    if (value.kind != JsonValue::Kind::object) {
        return std::unexpected(schema_error(path, value, std::string{name} + " must be a JSON object"));
    }
    return &value.object;
}

[[nodiscard]] std::expected<std::string, Error>
require_string(const fs::path& path, const JsonValue& value, std::string_view name) {
    if (value.kind != JsonValue::Kind::string) {
        return std::unexpected(schema_error(path, value, std::string{name} + " must be a JSON string"));
    }
    if (value.scalar.empty()) {
        return std::unexpected(schema_error(path, value, std::string{name} + " must not be empty"));
    }
    return value.scalar;
}

[[nodiscard]] std::expected<bool, Error>
require_bool(const fs::path& path, const JsonValue& value, std::string_view name) {
    if (value.kind != JsonValue::Kind::boolean) {
        return std::unexpected(schema_error(path, value, std::string{name} + " must be a JSON boolean"));
    }
    return value.boolean;
}

[[nodiscard]] std::expected<std::vector<std::string>, Error>
require_strings(const fs::path& path, const JsonValue& value, std::string_view name) {
    if (value.kind != JsonValue::Kind::array) {
        return std::unexpected(schema_error(path, value, std::string{name} + " must be a JSON array"));
    }
    std::vector<std::string> result;
    result.reserve(value.array.size());
    for (const auto& element : value.array) {
        auto text = require_string(path, element, name);
        if (!text) return std::unexpected(text.error());
        result.push_back(std::move(*text));
    }
    return result;
}

[[nodiscard]] fs::path resolve_path(const fs::path& root, const std::string& text) {
    fs::path value = fs::u8path(text);
    return (value.is_absolute() ? value : root / value).lexically_normal();
}

[[nodiscard]] std::expected<std::vector<fs::path>, Error>
require_paths(const fs::path& file, const fs::path& root, const JsonValue& value, std::string_view name) {
    auto strings = require_strings(file, value, name);
    if (!strings) return std::unexpected(strings.error());
    std::vector<fs::path> result;
    result.reserve(strings->size());
    for (const auto& text : *strings) result.push_back(resolve_path(root, text));
    return result;
}

[[nodiscard]] std::expected<void, Error> reject_unknown(
    const fs::path& file,
    const JsonObject& object,
    const std::vector<std::string_view>& allowed,
    std::string_view scope) {
    for (const auto& [key, value] : object) {
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
            return std::unexpected(schema_error(file, value, "unknown " + std::string{scope} + " field '" + key + "'"));
        }
    }
    return {};
}

[[nodiscard]] std::optional<BuildConfiguration> configuration(std::string_view value) {
    if (value == "debug") return BuildConfiguration::debug;
    if (value == "release") return BuildConfiguration::release;
    return std::nullopt;
}
[[nodiscard]] std::optional<Architecture> architecture(std::string_view value) {
    if (value == "x86") return Architecture::x86;
    if (value == "x64") return Architecture::x64;
    return std::nullopt;
}
[[nodiscard]] std::optional<CppStandard> standard(std::string_view value) {
    if (value == "14" || value == "c++14") return CppStandard::cpp14;
    if (value == "17" || value == "c++17") return CppStandard::cpp17;
    if (value == "20" || value == "c++20") return CppStandard::cpp20;
    if (value == "23" || value == "c++23") return CppStandard::cpp23;
    if (value == "latest" || value == "c++latest") return CppStandard::latest;
    return std::nullopt;
}
[[nodiscard]] std::optional<RuntimeLibrary> runtime_library(std::string_view value) {
    if (value == "MD" || value == "md") return RuntimeLibrary::md;
    if (value == "MDd" || value == "mdd") return RuntimeLibrary::mdd;
    if (value == "MT" || value == "mt") return RuntimeLibrary::mt;
    if (value == "MTd" || value == "mtd") return RuntimeLibrary::mtd;
    return std::nullopt;
}
[[nodiscard]] std::optional<LinkSubsystem> subsystem(std::string_view value) {
    if (value == "console") return LinkSubsystem::console;
    if (value == "windows") return LinkSubsystem::windows;
    return std::nullopt;
}
[[nodiscard]] std::optional<TargetKind> target_kind(std::string_view value) {
    if (value == "exe" || value == "executable") return TargetKind::executable;
    if (value == "dll" || value == "dynamic") return TargetKind::dynamic_library;
    return std::nullopt;
}

[[nodiscard]] std::expected<void, Error>
decode_build(const fs::path& file, const fs::path& root, const JsonValue& value, BuildOverrides& out) {
    auto object = require_object(file, value, "build");
    if (!object) return std::unexpected(object.error());
    auto known = reject_unknown(file, **object,
        {"configuration", "architecture", "standard", "runtime", "subsystem", "type", "output", "defines", "include_dirs",
         "library_dirs", "libraries", "compiler_args", "linker_args"},
        "build");
    if (!known) return std::unexpected(known.error());

    if (auto it = (**object).find("configuration"); it != (**object).end()) {
        auto text = require_string(file, it->second, "build.configuration");
        if (!text) return std::unexpected(text.error());
        auto parsed = configuration(*text);
        if (!parsed) return std::unexpected(schema_error(file, it->second, "build.configuration must be 'debug' or 'release'"));
        out.configuration = *parsed;
    }
    if (auto it = (**object).find("architecture"); it != (**object).end()) {
        auto text = require_string(file, it->second, "build.architecture");
        if (!text) return std::unexpected(text.error());
        auto parsed = architecture(*text);
        if (!parsed) return std::unexpected(schema_error(file, it->second, "build.architecture must be 'x86' or 'x64'"));
        out.architecture = *parsed;
    }
    if (auto it = (**object).find("standard"); it != (**object).end()) {
        auto text = require_string(file, it->second, "build.standard");
        if (!text) return std::unexpected(text.error());
        auto parsed = standard(*text);
        if (!parsed) return std::unexpected(schema_error(file, it->second, "build.standard must be '14', '17', '20', '23', or 'latest'"));
        out.standard = *parsed;
    }
    if (auto it = (**object).find("runtime"); it != (**object).end()) {
        auto text = require_string(file, it->second, "build.runtime");
        if (!text) return std::unexpected(text.error());
        auto parsed = runtime_library(*text);
        if (!parsed) return std::unexpected(schema_error(file, it->second, "build.runtime must be 'MD', 'MDd', 'MT', or 'MTd'"));
        out.runtime_library = *parsed;
    }
    if (auto it = (**object).find("subsystem"); it != (**object).end()) {
        auto text = require_string(file, it->second, "build.subsystem");
        if (!text) return std::unexpected(text.error());
        auto parsed = subsystem(*text);
        if (!parsed) return std::unexpected(schema_error(file, it->second, "build.subsystem must be 'console' or 'windows'"));
        out.subsystem = *parsed;
    }
    if (auto it = (**object).find("type"); it != (**object).end()) {
        auto text = require_string(file, it->second, "build.type");
        if (!text) return std::unexpected(text.error());
        auto parsed = target_kind(*text);
        if (!parsed) return std::unexpected(schema_error(
            file, it->second,
            "build.type must be 'exe' or 'dll'; static libraries require the librarian milestone"));
        out.target_kind = *parsed;
    }
    if (auto it = (**object).find("output"); it != (**object).end()) {
        auto text = require_string(file, it->second, "build.output");
        if (!text) return std::unexpected(text.error());
        out.output_name = std::move(*text);
    }
    if (auto it = (**object).find("defines"); it != (**object).end()) {
        auto values = require_strings(file, it->second, "build.defines");
        if (!values) return std::unexpected(values.error());
        out.defines = std::move(*values);
    }
    if (auto it = (**object).find("include_dirs"); it != (**object).end()) {
        auto values = require_paths(file, root, it->second, "build.include_dirs");
        if (!values) return std::unexpected(values.error());
        out.include_directories = std::move(*values);
    }
    if (auto it = (**object).find("library_dirs"); it != (**object).end()) {
        auto values = require_paths(file, root, it->second, "build.library_dirs");
        if (!values) return std::unexpected(values.error());
        out.library_directories = std::move(*values);
    }
    if (auto it = (**object).find("libraries"); it != (**object).end()) {
        auto values = require_strings(file, it->second, "build.libraries");
        if (!values) return std::unexpected(values.error());
        out.libraries = std::move(*values);
    }
    if (auto it = (**object).find("compiler_args"); it != (**object).end()) {
        auto values = require_strings(file, it->second, "build.compiler_args");
        if (!values) return std::unexpected(values.error());
        out.compiler_arguments = std::move(*values);
    }
    if (auto it = (**object).find("linker_args"); it != (**object).end()) {
        auto values = require_strings(file, it->second, "build.linker_args");
        if (!values) return std::unexpected(values.error());
        out.linker_arguments = std::move(*values);
    }
    return {};
}

[[nodiscard]] std::expected<void, Error>
decode_discovery(const fs::path& file, const fs::path& root, const JsonValue& value, DiscoveryOverrides& out) {
    auto object = require_object(file, value, "discovery");
    if (!object) return std::unexpected(object.error());
    auto known = reject_unknown(file, **object,
        {"enabled", "exclude_dirs", "extra_sources", "exclude_sources"}, "discovery");
    if (!known) return std::unexpected(known.error());

    if (auto it = (**object).find("enabled"); it != (**object).end()) {
        auto enabled = require_bool(file, it->second, "discovery.enabled");
        if (!enabled) return std::unexpected(enabled.error());
        out.enabled = *enabled;
    }
    if (auto it = (**object).find("exclude_dirs"); it != (**object).end()) {
        auto values = require_paths(file, root, it->second, "discovery.exclude_dirs");
        if (!values) return std::unexpected(values.error());
        out.exclude_directories = std::move(*values);
    }
    if (auto it = (**object).find("extra_sources"); it != (**object).end()) {
        auto values = require_paths(file, root, it->second, "discovery.extra_sources");
        if (!values) return std::unexpected(values.error());
        out.extra_sources = std::move(*values);
    }
    if (auto it = (**object).find("exclude_sources"); it != (**object).end()) {
        auto values = require_paths(file, root, it->second, "discovery.exclude_sources");
        if (!values) return std::unexpected(values.error());
        out.exclude_sources = std::move(*values);
    }
    return {};
}

} // namespace

std::expected<std::optional<fs::path>, Error>
ProjectConfigLoader::find_upwards(const fs::path& start_directory) {
    std::error_code ec;
    fs::path current = fs::absolute(start_directory, ec).lexically_normal();
    if (ec || !fs::is_directory(current, ec) || ec) {
        return std::unexpected(make_error(
            ErrorCode::io_error, start_directory, 1, 1,
            "project config search must start from an existing directory"));
    }
    while (true) {
        const fs::path candidate = current / "mqb.json";
        ec.clear();
        if (fs::exists(candidate, ec)) {
            if (ec || !fs::is_regular_file(candidate, ec) || ec) {
                return std::unexpected(make_error(
                    ErrorCode::io_error, candidate, 1, 1,
                    "mqb.json exists but is not a regular file"));
            }
            return std::optional<fs::path>{candidate.lexically_normal()};
        }
        const fs::path parent = current.parent_path();
        if (parent.empty() || parent == current) break;
        current = parent;
    }
    return std::optional<fs::path>{};
}

std::expected<ProjectConfig, Error>
ProjectConfigLoader::load(const fs::path& requested_file) {
    std::error_code ec;
    fs::path file = fs::absolute(requested_file, ec).lexically_normal();
    if (ec || !fs::is_regular_file(file, ec) || ec) {
        return std::unexpected(make_error(
            ErrorCode::io_error, requested_file, 1, 1,
            "project config must be an existing regular file"));
    }
    auto text = read_file(file);
    if (!text) return std::unexpected(text.error());
    JsonParser parser{*text, file};
    auto root = parser.parse();
    if (!root) return std::unexpected(root.error());
    auto object = require_object(file, *root, "project config root");
    if (!object) return std::unexpected(object.error());
    auto known = reject_unknown(file, **object, {"version", "build", "discovery"}, "root");
    if (!known) return std::unexpected(known.error());

    const auto version_it = (**object).find("version");
    if (version_it == (**object).end()) {
        return std::unexpected(schema_error(file, *root, "project config requires integer field 'version'"));
    }
    if (version_it->second.kind != JsonValue::Kind::number) {
        return std::unexpected(schema_error(file, version_it->second, "version must be integer 1"));
    }
    int version = 0;
    const auto& version_text = version_it->second.scalar;
    const auto [end, err] = std::from_chars(
        version_text.data(), version_text.data() + version_text.size(), version);
    if (err != std::errc{} || end != version_text.data() + version_text.size()) {
        return std::unexpected(schema_error(file, version_it->second, "version must be integer 1"));
    }
    if (version != 1) {
        return std::unexpected(make_error(
            ErrorCode::unsupported_version,
            file,
            version_it->second.line,
            version_it->second.column,
            "unsupported mqb.json version " + std::to_string(version) + " (expected 1)"));
    }

    ProjectConfig config;
    config.version = version;
    config.file = file;
    config.project_root = file.parent_path().lexically_normal();
    if (auto it = (**object).find("build"); it != (**object).end()) {
        auto decoded = decode_build(file, config.project_root, it->second, config.build);
        if (!decoded) return std::unexpected(decoded.error());
    }
    if (auto it = (**object).find("discovery"); it != (**object).end()) {
        auto decoded = decode_discovery(file, config.project_root, it->second, config.discovery);
        if (!decoded) return std::unexpected(decoded.error());
    }
    return config;
}

} // namespace mqb::config
