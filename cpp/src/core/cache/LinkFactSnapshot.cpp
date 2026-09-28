#include "mqb/core/LinkFactSnapshot.hpp"
#include "mqb/json/Json.hpp"

#include <array>
#include <charconv>
#include <initializer_list>
#include <limits>
#include <utility>

namespace mqb {
namespace {
using Code = LinkFactSnapshotErrorCode;
using Limits = LinkFactSnapshotLimits;
using Value = json::Value;
using Kind = json::Kind;
struct Rejected { LinkFactSnapshotError error; };
[[noreturn]] void reject(Code code, std::string message) { throw Rejected{{code, std::move(message)}}; }
void require(bool ok, std::string_view message) {
    if (!ok) reject(Code::invalid_record, std::string{message});
}
void limit(bool ok) { if (!ok) reject(Code::limit_exceeded, "snapshot resource limit exceeded"); }

// Encoding validation, not a second JSON grammar. Escapes/duplicates/grammar
// remain the responsibility of the existing json::parse implementation.
bool valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto b = static_cast<unsigned char>(text[i++]);
        if (b == 0) return false;
        if (b < 0x80) continue;
        unsigned remaining = 0;
        std::uint32_t cp = 0, minimum = 0;
        if (b >= 0xc2 && b <= 0xdf) { remaining = 1; cp = b & 0x1f; minimum = 0x80; }
        else if (b >= 0xe0 && b <= 0xef) { remaining = 2; cp = b & 0x0f; minimum = 0x800; }
        else if (b >= 0xf0 && b <= 0xf4) { remaining = 3; cp = b & 0x07; minimum = 0x10000; }
        else return false;
        if (remaining > text.size() - i) return false;
        while (remaining--) {
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (next & 0x3f);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    }
    return true;
}
void preflight(std::string_view text) {
    limit(text.size() <= Limits::document_bytes);
    if (text.starts_with("\xef\xbb\xbf") || !valid_utf8(text))
        reject(Code::invalid_document, "BOM, invalid UTF-8 or NUL");
    std::size_t depth = 0, tokens = 0;
    bool quoted = false, escaped = false;
    for (const char ch : text) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') quoted = false;
        } else if (ch == '"') quoted = true;
        else if (ch == '{' || ch == '[') { limit(++depth <= Limits::nesting); limit(++tokens <= Limits::structural_tokens); }
        else if (ch == '}' || ch == ']') {
            if (depth == 0) reject(Code::invalid_document, "unbalanced JSON container");
            --depth; limit(++tokens <= Limits::structural_tokens);
        } else if (ch == ',' || ch == ':') limit(++tokens <= Limits::structural_tokens);
    }
}

using namespace std::literals;
constexpr std::array completions{std::pair{ArtifactCompletion::executed, "executed"sv},
                                std::pair{ArtifactCompletion::reused, "reused"sv}};
constexpr std::array cache_states{std::pair{ArtifactCacheState::saved, "saved"sv},
    std::pair{ArtifactCacheState::reused, "reused"sv}, std::pair{ArtifactCacheState::save_failed, "save_failed"sv}};
constexpr std::array configurations{std::pair{BuildConfiguration::debug, "debug"sv},
                                  std::pair{BuildConfiguration::release, "release"sv}};
constexpr std::array architectures{std::pair{Architecture::x86, "x86"sv}, std::pair{Architecture::x64, "x64"sv}};
constexpr std::array targets{std::pair{TargetKind::executable, "executable"sv},
    std::pair{TargetKind::dynamic_library, "dynamic_library"sv}}; // LINK, not LIB records.
constexpr std::array warning_codes{std::pair{LinkFactWarningCode::cache_load_failed, "cache_load_failed"sv},
    std::pair{LinkFactWarningCode::cache_save_failed, "cache_save_failed"sv},
    std::pair{LinkFactWarningCode::file_snapshot_failed, "file_snapshot_failed"sv}};
using State = StorageFileObservationState;
constexpr std::array states{std::pair{State::not_attempted, "not_attempted"sv}, std::pair{State::observed, "observed"sv},
    std::pair{State::missing_leaf, "missing_leaf"sv}, std::pair{State::unavailable, "unavailable"sv},
    std::pair{State::invalid_path, "invalid_path"sv}, std::pair{State::reparse_rejected, "reparse_rejected"sv},
    std::pair{State::not_regular_file, "not_regular_file"sv}, std::pair{State::limit_exceeded, "limit_exceeded"sv}};
template<class E, std::size_t N> std::string_view enum_name(E value, const std::array<std::pair<E, std::string_view>, N>& names) {
    for (const auto& [key, name] : names) if (key == value) return name;
    reject(Code::invalid_record, "unknown snapshot enum");
}
template<class E, std::size_t N> E enum_value(const Value& value, const std::array<std::pair<E, std::string_view>, N>& names) {
    require(value.kind == Kind::string, "enum must be a string");
    for (const auto& [key, name] : names) if (name == value.scalar) return key;
    reject(Code::invalid_record, "unknown snapshot enum");
}
void fields(const Value& value, std::initializer_list<std::string_view> names) {
    require(value.kind == Kind::object && value.object.size() == names.size(), "missing or unknown fields");
    for (auto name : names) require(value.object.contains(name), "missing or unknown fields");
}
const Value& get(const Value& value, std::string_view name) { return value.object.find(name)->second; }
bool boolean(const Value& value) { require(value.kind == Kind::boolean, "boolean required"); return value.boolean; }
std::uint64_t integer(const Value& value, std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)()) {
    require(value.kind == Kind::number && !value.scalar.empty(), "unsigned integer required");
    for (char c : value.scalar) require(c >= '0' && c <= '9', "unsigned integer required");
    std::uint64_t out = 0;
    const auto result = std::from_chars(value.scalar.data(), value.scalar.data() + value.scalar.size(), out);
    require(result.ec == std::errc{} && result.ptr == value.scalar.data() + value.scalar.size() && out <= maximum,
            "unsigned integer out of range");
    return out;
}
template<class T> std::optional<T> optional_integer(const Value& value) {
    if (value.kind == Kind::null_value) return std::nullopt;
    return static_cast<T>(integer(value, (std::numeric_limits<T>::max)()));
}
std::string string(const Value& value) { require(value.kind == Kind::string, "string required"); return value.scalar; }
std::optional<std::string> optional_string(const Value& value) {
    if (value.kind == Kind::null_value) return std::nullopt;
    return string(value);
}
void array(const Value& value, std::size_t maximum) {
    require(value.kind == Kind::array, "array required"); limit(value.array.size() <= maximum);
}

void validate(const LinkFactSnapshot& v) {
    (void)enum_name(v.completion, completions); (void)enum_name(v.cache_state, cache_states);
    (void)enum_name(v.configuration, configurations); (void)enum_name(v.architecture, architectures);
    (void)enum_name(v.target_kind, targets); (void)enum_name(v.observation.state, states);
    const bool executed = v.completion == ArtifactCompletion::executed;
    require(v.linked == executed && (v.cache_state == ArtifactCacheState::reused) == !executed,
            "inconsistent completion, linked flag or cache state");
    require(!v.output.empty(), "successful record requires an output label");
    limit(v.warnings.size() <= Limits::warnings && v.observation.issues.size() <= Limits::issues);
    std::size_t total = 0;
    auto text = [&](const std::string& value) {
        limit(value.size() <= Limits::string_bytes && value.size() <= Limits::total_string_bytes - total);
        total += value.size(); require(valid_utf8(value), "invalid UTF-8 or NUL in a field");
    };
    if (v.capture_label) text(*v.capture_label);
    text(v.output); text(v.cache_file);
    if (v.working_directory) text(*v.working_directory);
    text(v.linker_path); text(v.linker_version); text(v.linker_stamp);
    for (const auto& w : v.warnings) { (void)enum_name(w.code, warning_codes); text(w.path); text(w.message); }
    const auto& o = v.observation;
    text(o.requested_path); text(o.physical_id);
    limit(o.opened_components <= StorageFileObservation::maximum_depth + 1);
    require(!o.hard_links || *o.hard_links > 0, "known hard-link count must be positive");
    require(o.observer_invoked || o.state == State::not_attempted || o.state == State::invalid_path,
            "observation state requires an invoked observer");
    if (o.state != State::observed)
        require(o.physical_id.empty() && !o.logical_bytes && !o.allocated_bytes && !o.hard_links,
                "non-observed result must not claim file metadata");
    else require(!o.requested_path.empty(), "observed result requires a requested path label");
    for (const auto& issue : o.issues) { text(issue.path); text(issue.message); }
}

// Schema-specific encoder; no dependency on app diagnostics or another parser.
struct Writer {
    std::string out;
    void add(std::string_view text) { limit(text.size() <= Limits::document_bytes - out.size()); out.append(text); }
    void text(std::string_view value) {
        constexpr char digits[] = "0123456789abcdef";
        add("\"");
        for (unsigned char c : value) {
            if (c == '"') add("\\\"");
            else if (c == '\\') add("\\\\");
            else if (c < 0x20) { const char escaped[]{'\\','u','0','0',digits[c >> 4],digits[c & 15]}; add({escaped, 6}); }
            else { const char byte = static_cast<char>(c); add({&byte, 1}); }
        }
        add("\"");
    }
    void number(std::uint64_t value) { add(std::to_string(value)); }
    void flag(bool value) { add(value ? "true" : "false"); }
    template<class T> void maybe_number(const std::optional<T>& value) { if (value) number(*value); else add("null"); }
    void maybe_text(const std::optional<std::string>& value) { if (value) text(*value); else add("null"); }
};
} // namespace

std::expected<void, LinkFactSnapshotError> validate_link_fact_snapshot(const LinkFactSnapshot& value) {
    try { validate(value); return {}; } catch (const Rejected& e) { return std::unexpected(e.error); }
}
std::expected<std::string, LinkFactSnapshotError> encode_link_fact_snapshot(const LinkFactSnapshot& v) {
    try {
        validate(v);
        Writer w;
        w.add(R"({"schema":"mqb.link-fact-snapshot","version":1,"provenance":"unverified-historical-facts","coverage":"link-main-output-v1","capture_label":)");
        w.maybe_text(v.capture_label);
        w.add(R"(,"build":{"completion":)"); w.text(enum_name(v.completion, completions));
        w.add(R"(,"cache_state":)"); w.text(enum_name(v.cache_state, cache_states));
        w.add(R"(,"linked":)"); w.flag(v.linked);
        w.add(R"(,"output":)"); w.text(v.output);
        w.add(R"(,"cache_file":)"); w.text(v.cache_file);
        w.add(R"(,"working_directory":)"); w.maybe_text(v.working_directory);
        w.add(R"(,"signature":{"high":)"); w.number(v.signature.high);
        w.add(R"(,"low":)"); w.number(v.signature.low);
        w.add(R"(},"linker":{"path":)"); w.text(v.linker_path);
        w.add(R"(,"version":)"); w.text(v.linker_version);
        w.add(R"(,"stamp":)"); w.text(v.linker_stamp);
        w.add(R"(},"configuration":)"); w.text(enum_name(v.configuration, configurations));
        w.add(R"(,"architecture":)"); w.text(enum_name(v.architecture, architectures));
        w.add(R"(,"target_kind":)"); w.text(enum_name(v.target_kind, targets));
        w.add(R"(,"warnings":[)");
        bool first = true;
        for (const auto& item : v.warnings) {
            if (!first) w.add(",");
            first = false;
            w.add(R"({"code":)"); w.text(enum_name(item.code, warning_codes));
            w.add(R"(,"path":)"); w.text(item.path);
            w.add(R"(,"message":)"); w.text(item.message); w.add("}");
        }
        const auto& o = v.observation;
        w.add(R"(]},"observation":{"observer_invoked":)"); w.flag(o.observer_invoked);
        w.add(R"(,"requested_path":)"); w.text(o.requested_path);
        w.add(R"(,"state":)"); w.text(enum_name(o.state, states));
        w.add(R"(,"physical_id":)"); w.text(o.physical_id);
        w.add(R"(,"logical_bytes":)"); w.maybe_number(o.logical_bytes);
        w.add(R"(,"allocated_bytes":)"); w.maybe_number(o.allocated_bytes);
        w.add(R"(,"hard_links":)"); w.maybe_number(o.hard_links);
        w.add(R"(,"opened_components":)"); w.number(o.opened_components);
        w.add(R"(,"issues":[)"); first = true;
        for (const auto& item : o.issues) {
            if (!first) w.add(",");
            first = false;
            w.add(R"({"path":)"); w.text(item.path);
            w.add(R"(,"message":)"); w.text(item.message);
            w.add(R"(,"native_code":)"); w.number(item.native_code); w.add("}");
        }
        w.add(R"(]},"authority":{"producer_identity_verified":false,"current_content_verified":false,"complete_producer_inventory":false,"deletion_authorized":false}})");
        return std::move(w.out);
    } catch (const Rejected& e) { return std::unexpected(e.error); }
}

std::expected<LinkFactSnapshot, LinkFactSnapshotError> decode_link_fact_snapshot(std::string_view text) {
    try {
        preflight(text);
        auto parsed = json::parse(text);
        if (!parsed) reject(Code::invalid_document, parsed.error().message);
        const auto& root = *parsed;
        fields(root, {"schema", "version", "provenance", "coverage", "capture_label", "build", "observation", "authority"});
        require(string(get(root, "schema")) == "mqb.link-fact-snapshot", "unknown schema");
        if (integer(get(root, "version")) != 1) reject(Code::unsupported_version, "unsupported snapshot version");
        require(string(get(root, "provenance")) == "unverified-historical-facts", "invalid provenance claim");
        require(string(get(root, "coverage")) == "link-main-output-v1", "unsupported coverage");
        const auto& a = get(root, "authority");
        fields(a, {"producer_identity_verified", "current_content_verified", "complete_producer_inventory", "deletion_authorized"});
        for (const auto& [key, value] : a.object) { (void)key; require(!boolean(value), "snapshot cannot grant authority"); }
        LinkFactSnapshot out;
        out.capture_label = optional_string(get(root, "capture_label"));
        const auto& b = get(root, "build");
        fields(b, {"completion", "cache_state", "linked", "output", "cache_file", "working_directory", "signature", "linker",
                   "configuration", "architecture", "target_kind", "warnings"});
        out.completion = enum_value(get(b, "completion"), completions);
        out.cache_state = enum_value(get(b, "cache_state"), cache_states);
        out.linked = boolean(get(b, "linked")); out.output = string(get(b, "output"));
        out.cache_file = string(get(b, "cache_file")); out.working_directory = optional_string(get(b, "working_directory"));
        const auto& signature = get(b, "signature"); fields(signature, {"high", "low"});
        out.signature = {integer(get(signature, "high")), integer(get(signature, "low"))};
        const auto& linker = get(b, "linker"); fields(linker, {"path", "version", "stamp"});
        out.linker_path = string(get(linker, "path")); out.linker_version = string(get(linker, "version"));
        out.linker_stamp = string(get(linker, "stamp"));
        out.configuration = enum_value(get(b, "configuration"), configurations);
        out.architecture = enum_value(get(b, "architecture"), architectures);
        out.target_kind = enum_value(get(b, "target_kind"), targets);
        const auto& warnings = get(b, "warnings"); array(warnings, Limits::warnings);
        for (const auto& item : warnings.array) {
            fields(item, {"code", "path", "message"});
            out.warnings.push_back({enum_value(get(item, "code"), warning_codes), string(get(item, "path")), string(get(item, "message"))});
        }
        const auto& o = get(root, "observation");
        fields(o, {"observer_invoked", "requested_path", "state", "physical_id", "logical_bytes", "allocated_bytes", "hard_links", "opened_components", "issues"});
        auto& result = out.observation;
        result.observer_invoked = boolean(get(o, "observer_invoked")); result.requested_path = string(get(o, "requested_path"));
        result.state = enum_value(get(o, "state"), states); result.physical_id = string(get(o, "physical_id"));
        result.logical_bytes = optional_integer<std::uint64_t>(get(o, "logical_bytes"));
        result.allocated_bytes = optional_integer<std::uint64_t>(get(o, "allocated_bytes"));
        result.hard_links = optional_integer<std::uint32_t>(get(o, "hard_links"));
        result.opened_components = static_cast<std::uint32_t>(integer(get(o, "opened_components"), StorageFileObservation::maximum_depth + 1));
        const auto& issues = get(o, "issues"); array(issues, Limits::issues);
        for (const auto& item : issues.array) {
            fields(item, {"path", "message", "native_code"});
            result.issues.push_back({string(get(item, "path")), string(get(item, "message")),
                static_cast<std::uint32_t>(integer(get(item, "native_code"), (std::numeric_limits<std::uint32_t>::max)()))});
        }
        validate(out);
        return out;
    } catch (const Rejected& e) { return std::unexpected(e.error); }
}
} // namespace mqb
