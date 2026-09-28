#include "mqb/orchestration/LinkFactSnapshotProjection.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace {
using namespace mqb;
using namespace mqb::orchestration;
unsigned checks = 0;
void check(bool ok, std::string_view message) {
    ++checks;
    if (!ok) throw std::runtime_error(std::string{message});
}
ObservedLinkResult fixture() {
    ObservedLinkResult v{
        .build = {.result = {}, .record = {
            .completion = ArtifactCompletion::executed, .cache_state = ArtifactCacheState::save_failed,
            .association = {.linker = {"C:/tools/link.exe", "14.51", "original-stamp"},
                            .signature = BuildSignature::from_digest({42, 9007199254740993ULL}),
                            .objects = {"input.obj"}, .output = std::filesystem::path{u8".mqb/bin/日本語 file.exe"}},
            .options = {}, .cache_file = ".mqb/cache/one.linkcache", .working_directory = "C:/original cwd"}},
        .observation = {}, .observer_invoked = true};
    v.build.result.linked = true;
    v.build.result.warnings = {{IncrementalLinkWarningCode::cache_save_failed, "cache", "unchanged original warning"}};
    v.observation.requested_path = std::filesystem::path{u8"C:/original cwd/.mqb/bin/日本語 file.exe"};
    v.observation.state = StorageFileObservationState::observed;
    v.observation.physical_id = "old-id";
    v.observation.logical_bytes = 46;
    v.observation.hard_links = 2;
    v.observation.opened_components = 5;
    return v;
}
void contracts() {
    static_assert(!std::is_convertible_v<LinkFactSnapshot, LinkCacheEntry>);
    auto source = fixture();
    const auto snapshot = capture_link_fact_snapshot(source, "caller-label");
    check(snapshot.has_value(), "capture already completed result");
    if (!snapshot) return;
    check(snapshot->signature == source.build.record.association.signature.digest(), "same record signature, no recomputation");
    const auto bytes = source.build.record.association.output.u8string();
    check(snapshot->output == std::string(bytes.begin(), bytes.end()), "UTF-8 path spelling preserved");
    check(snapshot->working_directory == "C:/original cwd", "recorded cwd, not current cwd");
    check(snapshot->capture_label == "caller-label", "caller label copied, no clock read");
    check(snapshot->warnings.size() == 1 && snapshot->warnings[0].message == "unchanged original warning", "warning preserved");
    source.observation.physical_id = "replacement-id"; source.build.result.warnings[0].message = "changed";
    check(snapshot->observation.physical_id == "old-id" && snapshot->warnings[0].message == "unchanged original warning", "owns history after input mutation");
    auto encoded = encode_link_fact_snapshot(*snapshot);
    auto restored = encoded ? decode_link_fact_snapshot(*encoded) : std::expected<LinkFactSnapshot, LinkFactSnapshotError>{std::unexpected(encoded.error())};
    check(restored && *restored == *snapshot, "projection codec roundtrip");
    check(!restored->deletion_authorized && !restored->current_content_verified, "readback remains unverified history");
    source = fixture(); source.build.record.working_directory.reset(); source.observation = {}; source.observer_invoked = false;
    auto absent = capture_link_fact_snapshot(source);
    check(absent && !absent->working_directory && !absent->observation.logical_bytes, "unknown values not guessed or zero-filled");
    source.build.record.completion = ArtifactCompletion::reused; source.build.record.cache_state = ArtifactCacheState::reused;
    source.build.result.linked = false;
    auto reused = capture_link_fact_snapshot(source);
    check(reused && reused->completion == ArtifactCompletion::reused && !reused->linked, "reuse never invents production");
    source.observer_invoked = true; source.observation.state = StorageFileObservationState::unavailable;
    source.observation.issues.push_back({"historical-request", "busy retained", 32});
    auto refused = capture_link_fact_snapshot(source);
    check(refused && refused->observation.state == StorageFileObservationState::unavailable &&
          refused->observation.issues[0].native_code == 32, "observation refusal distinct from successful reuse");
    source.build.result.linked = true;
    check(!capture_link_fact_snapshot(source), "inconsistent constructed completion rejected");
    source = fixture(); source.build.result.warnings[0].code = static_cast<IncrementalLinkWarningCode>(999);
    check(!capture_link_fact_snapshot(source), "unknown warning enum refused");
    source = fixture(); source.observation.opened_components = (std::numeric_limits<std::size_t>::max)();
    check(!capture_link_fact_snapshot(source), "component count checked before narrowing");
    source = fixture(); source.build.result.warnings.resize(65);
    check(!capture_link_fact_snapshot(source), "capture warning count budget before copying");
    source = fixture(); source.observation.issues.resize(129);
    check(!capture_link_fact_snapshot(source), "capture issue count budget before copying");
    source = fixture(); source.build.record.association.linker.version.assign(131073, 'a');
    check(!capture_link_fact_snapshot(source), "string bound checked before retaining metadata");
    source = fixture(); source.build.result.warnings.assign(5, {IncrementalLinkWarningCode::cache_save_failed, "cache", std::string(131072, 'x')});
    check(!capture_link_fact_snapshot(source), "aggregate capture bound before retaining all warnings");
    source = fixture(); std::string annotation = "owned label";
    const auto labelled = capture_link_fact_snapshot(source, annotation);
    annotation = "changed";
    check(labelled && labelled->capture_label == "owned label", "capture annotation copied from view");
    source = fixture();
    source.build.record.association.output = std::filesystem::path{u8"a/../日本語.exe"};
    auto lexical = capture_link_fact_snapshot(source);
    check(lexical && lexical->output.find("..") != std::string::npos, "capture never normalizes labels into safety claims");
    source = fixture();
#ifdef _WIN32
    // Native UTF-16 input can be stored by path without being valid Unicode.
    source.build.record.association.output = std::filesystem::path{std::wstring(1, L'\xd800')};
#else
    source.build.record.association.output = std::filesystem::path{std::string(1, static_cast<char>(0xff))};
#endif
    auto invalid_path = capture_link_fact_snapshot(source);
    check(!invalid_path && invalid_path.error().code == LinkFactSnapshotErrorCode::invalid_record,
          "invalid native path encoding refused without a filesystem probe");
}
}
int main() {
    try { contracts(); std::cout << "link fact projection: " << checks << " checks passed; zero build/observer/file IO\n"; }
    catch (const std::exception& e) { std::cerr << "check " << checks << ": " << e.what() << '\n'; return 1; }
}
