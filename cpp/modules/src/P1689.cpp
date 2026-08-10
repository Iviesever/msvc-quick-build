#include "mqb/modules/P1689.hpp"

#include <charconv>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "mqb/json/Json.hpp"

namespace mqb::modules {
namespace {

namespace fs = std::filesystem;
using json::Kind;
using json::Object;
using json::Value;

[[nodiscard]] P1689Error schema_error(
    const Value& value,
    std::string message) {
    return P1689Error{
        .code = P1689ErrorCode::schema_error,
        .line = value.line,
        .column = value.column,
        .message = std::move(message),
    };
}

[[nodiscard]] bool vendor_extension(const std::string_view key) noexcept {
    return key.size() >= 3 && key.front() == '_'
        && key.find('_', 1) != std::string_view::npos;
}

[[nodiscard]] bool allowed_key(
    const std::string_view key,
    const std::initializer_list<std::string_view> allowed) {
    for (const auto candidate : allowed) {
        if (key == candidate) return true;
    }
    return vendor_extension(key);
}

[[nodiscard]] std::expected<void, P1689Error> reject_unknown(
    const Object& object,
    const std::initializer_list<std::string_view> allowed,
    const std::string_view scope) {
    for (const auto& [key, value] : object) {
        if (!allowed_key(key, allowed)) {
            return std::unexpected(schema_error(
                value,
                "unknown P1689 " + std::string{scope} + " field '" + key + "'"));
        }
    }
    return {};
}

[[nodiscard]] std::expected<const Object*, P1689Error> require_object(
    const Value& value,
    const std::string_view name) {
    if (value.kind != Kind::object) {
        return std::unexpected(schema_error(
            value,
            std::string{name} + " must be a JSON object"));
    }
    return &value.object;
}

[[nodiscard]] std::expected<const std::vector<Value>*, P1689Error> require_array(
    const Value& value,
    const std::string_view name) {
    if (value.kind != Kind::array) {
        return std::unexpected(schema_error(
            value,
            std::string{name} + " must be a JSON array"));
    }
    return &value.array;
}

[[nodiscard]] std::expected<std::string, P1689Error> require_string(
    const Value& value,
    const std::string_view name) {
    if (value.kind != Kind::string) {
        return std::unexpected(schema_error(
            value,
            std::string{name} + " must be a JSON string"));
    }
    if (value.scalar.empty()) {
        return std::unexpected(schema_error(
            value,
            std::string{name} + " must not be empty"));
    }
    return value.scalar;
}

[[nodiscard]] std::expected<bool, P1689Error> require_bool(
    const Value& value,
    const std::string_view name) {
    if (value.kind != Kind::boolean) {
        return std::unexpected(schema_error(
            value,
            std::string{name} + " must be a JSON boolean"));
    }
    return value.boolean;
}

[[nodiscard]] std::expected<int, P1689Error> require_integer(
    const Value& value,
    const std::string_view name) {
    if (value.kind != Kind::number) {
        return std::unexpected(schema_error(
            value,
            std::string{name} + " must be a JSON integer"));
    }

    int result = 0;
    const auto* begin = value.scalar.data();
    const auto* end = begin + value.scalar.size();
    const auto [parsed_end, error] = std::from_chars(begin, end, result);
    if (error != std::errc{} || parsed_end != end) {
        return std::unexpected(schema_error(
            value,
            std::string{name} + " must be a JSON integer"));
    }
    return result;
}

[[nodiscard]] fs::path utf8_path(const std::string& value) {
    std::u8string bytes;
    bytes.reserve(value.size());
    for (const unsigned char byte : value) {
        bytes.push_back(static_cast<char8_t>(byte));
    }
    return fs::path{bytes};
}

[[nodiscard]] std::expected<fs::path, P1689Error> require_path(
    const Value& value,
    const std::string_view name) {
    auto text = require_string(value, name);
    if (!text) return std::unexpected(text.error());
    return utf8_path(*text);
}

[[nodiscard]] std::expected<std::vector<fs::path>, P1689Error> require_paths(
    const Value& value,
    const std::string_view name) {
    auto array = require_array(value, name);
    if (!array) return std::unexpected(array.error());

    std::vector<fs::path> paths;
    paths.reserve((*array)->size());
    for (const auto& element : **array) {
        auto path = require_path(element, name);
        if (!path) return std::unexpected(path.error());
        paths.push_back(std::move(*path));
    }
    return paths;
}

[[nodiscard]] std::expected<LookupMethod, P1689Error> decode_lookup_method(
    const Value& value) {
    auto text = require_string(value, "requires.lookup-method");
    if (!text) return std::unexpected(text.error());
    if (*text == "by-name") return LookupMethod::by_name;
    if (*text == "include-angle") return LookupMethod::include_angle;
    if (*text == "include-quote") return LookupMethod::include_quote;
    return std::unexpected(schema_error(
        value,
        "requires.lookup-method must be 'by-name', 'include-angle', or 'include-quote'"));
}

struct CommonModuleFields {
    std::string logical_name;
    std::optional<fs::path> source_path;
    std::optional<fs::path> compiled_module_path;
    bool unique_on_source_path{false};
};

[[nodiscard]] std::expected<CommonModuleFields, P1689Error> decode_common_module(
    const Object& object,
    const Value& object_value,
    const std::string_view scope) {
    const auto logical = object.find("logical-name");
    if (logical == object.end()) {
        return std::unexpected(schema_error(
            object_value,
            std::string{scope} + " requires non-empty 'logical-name'"));
    }

    auto logical_name = require_string(logical->second, std::string{scope} + ".logical-name");
    if (!logical_name) return std::unexpected(logical_name.error());

    CommonModuleFields result;
    result.logical_name = std::move(*logical_name);

    if (const auto it = object.find("source-path"); it != object.end()) {
        auto path = require_path(it->second, std::string{scope} + ".source-path");
        if (!path) return std::unexpected(path.error());
        result.source_path = std::move(*path);
    }
    if (const auto it = object.find("compiled-module-path"); it != object.end()) {
        auto path = require_path(it->second, std::string{scope} + ".compiled-module-path");
        if (!path) return std::unexpected(path.error());
        result.compiled_module_path = std::move(*path);
    }
    if (const auto it = object.find("unique-on-source-path"); it != object.end()) {
        auto unique = require_bool(it->second, std::string{scope} + ".unique-on-source-path");
        if (!unique) return std::unexpected(unique.error());
        result.unique_on_source_path = *unique;
    }

    if (result.unique_on_source_path && !result.source_path) {
        return std::unexpected(schema_error(
            object_value,
            std::string{scope}
                + " with unique-on-source-path=true requires non-empty 'source-path'"));
    }
    return result;
}

[[nodiscard]] std::expected<ProvidedModule, P1689Error> decode_provided(
    const Value& value) {
    auto object = require_object(value, "provides entry");
    if (!object) return std::unexpected(object.error());
    auto known = reject_unknown(
        **object,
        {"logical-name", "source-path", "compiled-module-path", "unique-on-source-path", "is-interface"},
        "provides");
    if (!known) return std::unexpected(known.error());

    auto common = decode_common_module(**object, value, "provides");
    if (!common) return std::unexpected(common.error());

    bool is_interface = true;
    if (const auto it = (**object).find("is-interface"); it != (**object).end()) {
        auto parsed = require_bool(it->second, "provides.is-interface");
        if (!parsed) return std::unexpected(parsed.error());
        is_interface = *parsed;
    }

    return ProvidedModule{
        .logical_name = std::move(common->logical_name),
        .source_path = std::move(common->source_path),
        .compiled_module_path = std::move(common->compiled_module_path),
        .unique_on_source_path = common->unique_on_source_path,
        .is_interface = is_interface,
    };
}

[[nodiscard]] std::expected<RequiredModule, P1689Error> decode_required(
    const Value& value) {
    auto object = require_object(value, "requires entry");
    if (!object) return std::unexpected(object.error());
    auto known = reject_unknown(
        **object,
        {"logical-name", "source-path", "compiled-module-path", "unique-on-source-path", "lookup-method"},
        "requires");
    if (!known) return std::unexpected(known.error());

    auto common = decode_common_module(**object, value, "requires");
    if (!common) return std::unexpected(common.error());

    LookupMethod lookup_method = LookupMethod::by_name;
    if (const auto it = (**object).find("lookup-method"); it != (**object).end()) {
        auto parsed = decode_lookup_method(it->second);
        if (!parsed) return std::unexpected(parsed.error());
        lookup_method = *parsed;
    }

    return RequiredModule{
        .logical_name = std::move(common->logical_name),
        .source_path = std::move(common->source_path),
        .compiled_module_path = std::move(common->compiled_module_path),
        .unique_on_source_path = common->unique_on_source_path,
        .lookup_method = lookup_method,
    };
}

[[nodiscard]] std::expected<P1689Rule, P1689Error> decode_rule(const Value& value) {
    auto object = require_object(value, "P1689 rule");
    if (!object) return std::unexpected(object.error());
    auto known = reject_unknown(
        **object,
        {"work-directory", "primary-output", "outputs", "provides", "requires"},
        "rule");
    if (!known) return std::unexpected(known.error());

    P1689Rule rule;
    if (const auto it = (**object).find("work-directory"); it != (**object).end()) {
        auto path = require_path(it->second, "rule.work-directory");
        if (!path) return std::unexpected(path.error());
        rule.work_directory = std::move(*path);
    }
    if (const auto it = (**object).find("primary-output"); it != (**object).end()) {
        auto path = require_path(it->second, "rule.primary-output");
        if (!path) return std::unexpected(path.error());
        rule.primary_output = std::move(*path);
    }
    if (const auto it = (**object).find("outputs"); it != (**object).end()) {
        auto paths = require_paths(it->second, "rule.outputs");
        if (!paths) return std::unexpected(paths.error());
        rule.outputs = std::move(*paths);
    }
    if (const auto it = (**object).find("provides"); it != (**object).end()) {
        auto array = require_array(it->second, "rule.provides");
        if (!array) return std::unexpected(array.error());
        rule.provides.reserve((*array)->size());
        for (const auto& entry : **array) {
            auto provided = decode_provided(entry);
            if (!provided) return std::unexpected(provided.error());
            rule.provides.push_back(std::move(*provided));
        }
    }
    if (const auto it = (**object).find("requires"); it != (**object).end()) {
        auto array = require_array(it->second, "rule.requires");
        if (!array) return std::unexpected(array.error());
        rule.requires.reserve((*array)->size());
        for (const auto& entry : **array) {
            auto required = decode_required(entry);
            if (!required) return std::unexpected(required.error());
            rule.requires.push_back(std::move(*required));
        }
    }
    return rule;
}

} // namespace

std::expected<P1689Document, P1689Error>
P1689Parser::parse(const std::string_view text) {
    auto root = json::parse(text);
    if (!root) {
        return std::unexpected(P1689Error{
            .code = P1689ErrorCode::parse_error,
            .line = root.error().line,
            .column = root.error().column,
            .message = root.error().message,
        });
    }

    auto object = require_object(*root, "P1689 root");
    if (!object) return std::unexpected(object.error());
    auto known = reject_unknown(**object, {"version", "revision", "rules"}, "root");
    if (!known) return std::unexpected(known.error());

    const auto version_it = (**object).find("version");
    if (version_it == (**object).end()) {
        return std::unexpected(schema_error(*root, "P1689 root requires integer field 'version'"));
    }
    auto version = require_integer(version_it->second, "version");
    if (!version) return std::unexpected(version.error());
    if (*version != 1) {
        return std::unexpected(P1689Error{
            .code = P1689ErrorCode::unsupported_version,
            .line = version_it->second.line,
            .column = version_it->second.column,
            .message = "unsupported P1689 version " + std::to_string(*version) + " (expected 1)",
        });
    }

    int revision = 0;
    if (const auto revision_it = (**object).find("revision"); revision_it != (**object).end()) {
        auto parsed = require_integer(revision_it->second, "revision");
        if (!parsed) return std::unexpected(parsed.error());
        revision = *parsed;
        if (revision != 0) {
            return std::unexpected(P1689Error{
                .code = P1689ErrorCode::unsupported_revision,
                .line = revision_it->second.line,
                .column = revision_it->second.column,
                .message = "unsupported P1689 revision " + std::to_string(revision) + " (expected 0)",
            });
        }
    }

    const auto rules_it = (**object).find("rules");
    if (rules_it == (**object).end()) {
        return std::unexpected(schema_error(*root, "P1689 root requires non-empty array field 'rules'"));
    }
    auto rules = require_array(rules_it->second, "rules");
    if (!rules) return std::unexpected(rules.error());
    if ((*rules)->empty()) {
        return std::unexpected(schema_error(rules_it->second, "rules must contain at least one rule"));
    }

    P1689Document document;
    document.version = *version;
    document.revision = revision;
    document.rules.reserve((*rules)->size());
    for (const auto& rule_value : **rules) {
        auto rule = decode_rule(rule_value);
        if (!rule) return std::unexpected(rule.error());
        document.rules.push_back(std::move(*rule));
    }
    return document;
}

} // namespace mqb::modules
