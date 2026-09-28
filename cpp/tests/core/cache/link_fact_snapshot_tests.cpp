#include "mqb/core/LinkFactSnapshot.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace mqb;
using State = StorageFileObservationState;
unsigned checks = 0;
void check(bool ok, std::string_view message) {
    ++checks;
    if (!ok) throw std::runtime_error(std::string{message});
}
std::string replace(std::string input, std::string_view from, std::string_view to) {
    const auto position = input.find(from);
    if (position == std::string::npos) throw std::runtime_error("missing mutation anchor");
    input.replace(position, from.size(), to);
    return input;
}
LinkFactSnapshot fixture() {
    LinkFactSnapshot v;
    v.capture_label = "caller clock label, not verified";
    v.completion = ArtifactCompletion::executed;
    v.cache_state = ArtifactCacheState::save_failed;
    v.linked = true;
    v.output = "bin/日本語 build \"x\".exe";
    v.cache_file = ".mqb/cache/app.linkcache";
    v.working_directory = R"(C:\work dir\project)";
    v.signature = {0xffffffffffffffffULL, 9007199254740993ULL};
    v.linker_path = "C:/tools/link.exe";
    v.linker_version = "14.51";
    v.linker_stamp = "opaque stamp";
    v.warnings = {{LinkFactWarningCode::cache_load_failed, v.cache_file, "load warning\nkept"},
                  {LinkFactWarningCode::cache_save_failed, v.cache_file, "save warning"},
                  {LinkFactWarningCode::file_snapshot_failed, "input", "original \t message"}};
    auto& o = v.observation;
    o.observer_invoked = true;
    o.requested_path = R"(C:\work dir\project\bin\日本語 build "x".exe)";
    o.state = State::observed;
    o.physical_id = "historical-volume:file-id";
    o.logical_bytes = 0;
    o.allocated_bytes = std::nullopt;
    o.hard_links = 2;
    o.opened_components = 4;
    o.issues = {{o.requested_path, "allocation unavailable", 0}};
    return v;
}
std::string encode(const LinkFactSnapshot& v) {
    auto text = encode_link_fact_snapshot(v);
    if (!text) throw std::runtime_error(text.error().message);
    return std::move(*text);
}
void roundtrip(const LinkFactSnapshot& v) {
    const auto encoded = encode(v);
    const auto restored = decode_link_fact_snapshot(encoded);
    check(restored && *restored == v, "owned snapshot must roundtrip exactly");
    check(restored && encode(*restored) == encoded, "canonical encoder must be deterministic");
}
void contracts() {
    static_assert(!LinkFactSnapshot::producer_identity_verified);
    static_assert(!LinkFactSnapshot::current_content_verified);
    static_assert(!LinkFactSnapshot::complete_producer_inventory);
    static_assert(!LinkFactSnapshot::deletion_authorized);
    auto v = fixture();
    roundtrip(v);
    const auto good = encode(v);
    check(good.find("18446744073709551615") != std::string::npos &&
          good.find("9007199254740993") != std::string::npos, "integer references are never doubles");
    v.completion = ArtifactCompletion::reused; v.cache_state = ArtifactCacheState::reused; v.linked = false;
    v.configuration = BuildConfiguration::release; v.architecture = Architecture::x86; v.target_kind = TargetKind::dynamic_library;
    v.capture_label.reset(); v.working_directory.reset();
    roundtrip(v);
    v = fixture(); v.cache_state = ArtifactCacheState::saved; roundtrip(v);
    // Every refusal state preserves missing metadata rather than inventing zeros.
    for (auto state : {State::not_attempted, State::missing_leaf, State::unavailable,
                       State::invalid_path, State::reparse_rejected, State::not_regular_file, State::limit_exceeded}) {
        v = fixture(); v.observation = {};
        v.observation.state = state; v.observation.observer_invoked = true;
        v.observation.requested_path = "history/../requested.exe";
        v.observation.issues.push_back({"missing parent", "refusal detail", 32});
        roundtrip(v);
    }
    v.observation.observer_invoked = false; v.observation.state = State::invalid_path; roundtrip(v);
    v.observation.state = State::not_attempted; roundtrip(v);
    v = fixture(); v.observation.logical_bytes.reset(); v.observation.hard_links.reset();
    v.observation.physical_id.clear(); roundtrip(v); // Missing fields remain unknown even for supplied observed state.
    v = fixture(); v.observation.logical_bytes = (std::numeric_limits<std::uint64_t>::max)();
    v.observation.allocated_bytes = (std::numeric_limits<std::uint64_t>::max)();
    v.observation.hard_links = (std::numeric_limits<std::uint32_t>::max)();
    v.observation.issues[0].native_code = (std::numeric_limits<std::uint32_t>::max)();
    v.observation.opened_components = 129; roundtrip(v);
    v = fixture();
    auto owned = decode_link_fact_snapshot(encode(v));
    v.observation.physical_id = "later replacement"; v.warnings[0].message = "mutated";
    check(owned && owned->observation.physical_id == "historical-volume:file-id" &&
          owned->warnings[0].message == "load warning\nkept", "snapshot does not alias input or imply current identity");
    for (const auto& [from, to] : {
        std::pair{"\"version\":1", "\"version\":2"},
        {"\"version\":1", "\"version\":1.0"}, {"\"version\":1", "\"version\":true"},
        {"\"schema\":\"mqb.link-fact-snapshot\"", "\"schema\":\"mqb.link-cache\""},
        {"unverified-historical-facts", "verified-live-facts"}, {"link-main-output-v1", "complete-inventory"},
        {"\"completion\":\"executed\"", "\"completion\":\"failed\""},
        {"\"cache_state\":\"save_failed\"", "\"cache_state\":\"reused\""},
        {"\"linked\":true", "\"linked\":false"}, {"\"linked\":true", "\"linked\":1"},
        {"\"configuration\":\"debug\"", "\"configuration\":\"fast\""},
        {"\"architecture\":\"x64\"", "\"architecture\":\"arm64\""},
        {"\"target_kind\":\"executable\"", "\"target_kind\":\"static_library\""},
        {"\"state\":\"observed\"", "\"state\":\"missing_leaf\""},
        {"\"state\":\"observed\"", "\"state\":\"unrecognized\""},
        {"\"observer_invoked\":true", "\"observer_invoked\":false"},
        {"\"hard_links\":2", "\"hard_links\":0"}, {"\"hard_links\":2", "\"hard_links\":4294967296"},
        {"\"native_code\":0", "\"native_code\":-1"}, {"\"native_code\":0", "\"native_code\":4294967296"},
        {"\"opened_components\":4", "\"opened_components\":130"},
        {"\"logical_bytes\":0", "\"logical_bytes\":18446744073709551616"},
        {"\"logical_bytes\":0", "\"logical_bytes\":-0"}, {"\"logical_bytes\":0", "\"logical_bytes\":1e2"},
        {"\"logical_bytes\":0", "\"logical_bytes\":0.5"}, {"\"logical_bytes\":0", "\"logical_bytes\":\"0\""},
        {"\"logical_bytes\":0", "\"logical_bytes\":true"}, {"\"logical_bytes\":0", "\"logical_bytes\":00"},
        {"cache_load_failed", "unknown_warning"}, {"opaque stamp", "\\u0000"},
        {"opaque stamp", "\\ud800"}, {"opaque stamp", "\\udc00"},
        {"\"version\":1", "\"version\":1,\"version\":1"},
        {"\"version\":1", "\"version\":1,\"ver\\u0073ion\":1"},
        {"\"high\":18446744073709551615", "\"high\":1,\"high\":1"},
        {"\"version\":1,", ""}, {"\"version\":1", "\"version\":1,\"new_field\":null"},
        {"\"native_code\":0", "\"native_code\":0,\"unknown\":0"}
    }) check(!decode_link_fact_snapshot(replace(good, from, to)), "malformed or contradictory input must fail");
    for (const std::string name : {"producer_identity_verified", "current_content_verified", "complete_producer_inventory", "deletion_authorized"}) {
        const auto field = "\"" + name + "\":false";
        for (const std::string replacement : {"true", "1", "\"false\"", "null"})
            check(!decode_link_fact_snapshot(replace(good, field, "\"" + name + "\":" + replacement)), "authority is exact false, not coercible");
    }
    for (std::size_t i = 0; i < good.size(); ++i)
        check(!decode_link_fact_snapshot(std::string_view{good}.substr(0, i)), "every truncated prefix must fail");
    for (const std::string suffix : {"{}", "null", ",", "x"})
        check(!decode_link_fact_snapshot(good + suffix), "trailing garbage must fail");
    check(decode_link_fact_snapshot(" \r\n" + good + "\t ").has_value(), "JSON whitespace allowed");
    check(!decode_link_fact_snapshot(std::string{"\xef\xbb\xbf"} + good), "BOM is not silently stripped");
    for (const std::string& invalid : {std::string{"\xc0\xaf"}, std::string{"\xed\xa0\x80"},
            std::string{"\xf4\x90\x80\x80"}, std::string{"\x80"}, std::string{"\xe2\x82"}, std::string(1, '\0')})
        check(!decode_link_fact_snapshot(replace(good, "opaque stamp", invalid)), "invalid raw UTF-8 refused");
    auto escaped = replace(good, "opaque stamp", R"(\ud83d\ude00)");
    auto decoded = decode_link_fact_snapshot(escaped);
    check(decoded && decoded->linker_stamp == "\xf0\x9f\x98\x80", "valid surrogate pair decoded losslessly");
    if (decoded) roundtrip(*decoded);
    // Resource limits are enforced on incoming documents AND caller-built models.
    check(!decode_link_fact_snapshot(std::string(LinkFactSnapshotLimits::document_bytes + 1, ' ')), "byte budget");
    check(!decode_link_fact_snapshot(std::string(13, '[') + "0" + std::string(13, ']')), "depth budget before recursive parse");
    std::string tokens = "[";
    for (unsigned i = 0; i < 4100; ++i) tokens += "0,";
    tokens += "0]";
    check(!decode_link_fact_snapshot(tokens), "structural token budget before allocations");
    v = fixture(); v.linker_stamp.assign(LinkFactSnapshotLimits::string_bytes, 'a'); roundtrip(v);
    v.linker_stamp += 'b'; check(!encode_link_fact_snapshot(v), "single-string encoder bound");
    check(!decode_link_fact_snapshot(replace(good, "opaque stamp", v.linker_stamp)), "single-string decoder bound");
    v = fixture();
    // assign(count, value) requires value not to refer into the destination.
    // Keep an owned copy: reallocation may destroy the old vector elements.
    const auto warning_sample = v.warnings.front();
    v.warnings.assign(LinkFactSnapshotLimits::warnings, warning_sample);
    check(std::all_of(v.warnings.begin(), v.warnings.end(),
        [&](const auto& item) { return item == warning_sample; }), "warning fixture copies retain all fields");
    roundtrip(v);
    v.warnings.push_back(warning_sample); check(!encode_link_fact_snapshot(v), "warning count budget");
    v = fixture();
    const auto issue_sample = v.observation.issues.front();
    v.observation.issues.assign(LinkFactSnapshotLimits::issues, issue_sample);
    check(std::all_of(v.observation.issues.begin(), v.observation.issues.end(),
        [&](const auto& item) { return item == issue_sample; }), "issue fixture copies retain all fields");
    roundtrip(v);
    v.observation.issues.push_back(issue_sample); check(!encode_link_fact_snapshot(v), "issue count budget");
    v = fixture(); v.warnings.clear(); v.observation.issues.clear();
    v.output.assign(131072, 'x'); v.linker_path.assign(131072, 'x'); v.linker_version.assign(131072, 'x'); v.linker_stamp.assign(131072, 'x');
    check(!encode_link_fact_snapshot(v), "aggregate string budget");
    check(decode_link_fact_snapshot(good + std::string(LinkFactSnapshotLimits::document_bytes - good.size(), ' ')).has_value(),
          "document exactly at byte budget is accepted");
    std::string many_warnings = "\"warnings\":[";
    for (unsigned i = 0; i < 65; ++i)
        many_warnings += R"({"code":"cache_load_failed","path":"","message":""},)";
    check(!decode_link_fact_snapshot(replace(good, "\"warnings\":[", many_warnings)), "decoder warning budget");
    std::string many_issues = "\"issues\":[";
    for (unsigned i = 0; i < 129; ++i)
        many_issues += R"({"path":"","message":"","native_code":0},)";
    check(!decode_link_fact_snapshot(replace(good, "\"issues\":[", many_issues)), "decoder issue budget");
    const std::string large(131072, 'a');
    auto excessive = replace(good, "opaque stamp", large);
    excessive = replace(excessive, "C:/tools/link.exe", large);
    excessive = replace(excessive, "14.51", large);
    excessive = replace(excessive, "caller clock label, not verified", large);
    check(!decode_link_fact_snapshot(excessive), "decoder aggregate string budget");
    v = fixture();
    v.output.assign(50000, '\1'); v.linker_path.assign(50000, '\1');
    v.linker_version.assign(50000, '\1'); v.linker_stamp.assign(50000, '\1');
    check(validate_link_fact_snapshot(v).has_value(), "bounded decoded string model");
    check(!encode_link_fact_snapshot(v), "escaped expansion cannot exceed output budget");
    // Deterministic corruptions: acceptance must still result in canonical, stable history.
    for (std::size_t i = 0; i < good.size(); i += 7) {
        auto damaged = good; damaged[i] = static_cast<char>((i * 37) % 127 + 1);
        auto read = decode_link_fact_snapshot(damaged);
        if (read) roundtrip(*read);
        else check(true, "mutation refused without a crash");
    }
    v = fixture(); v.linker_stamp = std::string(1, '\0'); check(!encode_link_fact_snapshot(v), "NUL model rejected");
    v = fixture(); v.completion = static_cast<ArtifactCompletion>(999); check(!encode_link_fact_snapshot(v), "unknown typed enum refused");
    v = fixture(); v.observation.hard_links = 0; check(!encode_link_fact_snapshot(v), "contradictory typed metadata refused");
}
}
int main() {
    try { contracts(); std::cout << "link fact snapshot: " << checks << " checks passed (includes exhaustive truncation prefixes)\n"; }
    catch (const std::exception& e) { std::cerr << "check " << checks << ": " << e.what() << '\n'; return 1; }
}
