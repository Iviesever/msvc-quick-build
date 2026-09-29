#include "../../../src/orchestration/incremental/LinkCompletionArchiveOperation.hpp"
#include <iostream>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace mqb;
using namespace mqb::orchestration;
using namespace mqb::platform::windows;
using Stored = std::expected<LinkFactFileReceipt, LinkFactFileError>;
unsigned checks{};
void check(bool ok, const char* text) { ++checks; if (!ok) throw std::runtime_error(text); }
ObservedLinkResult fixture() {
    ObservedLinkResult out{.build = {.result = {}, .record = {
        .completion = ArtifactCompletion::executed, .cache_state = ArtifactCacheState::save_failed,
        .association = {.linker = {"link.exe", "original", "stamp"},
            .signature = BuildSignature::from_digest({7, 9}), .objects = {"one.obj"}, .output = "original.exe", .libraries = {}, .file_inputs = {}, .side_outputs = {}},
        .options = {}, .cache_file = "original.cache", .working_directory = "C:/original"}},
        .observation = {}, .observer_invoked = false};
    out.build.result.linked = true;
    out.build.result.warnings = {{IncrementalLinkWarningCode::cache_load_failed, "original.cache", "load"},
        {IncrementalLinkWarningCode::cache_save_failed, "original.cache", "save"}};
    out.build.result.process = process::ProcessResult{0, "original stdout", "original stderr",
        std::chrono::nanoseconds{123}, process::ProcessTermination::exited};
    return out;
}
void run() {
    static_assert(std::is_same_v<decltype(&archive_link_completion), LinkCompletionArchiveResult(*)(
        const ObservedLinkResult&, const std::filesystem::path&, std::optional<std::string_view>)>);
    const auto completed = fixture();
    const auto original = capture_link_fact_snapshot(completed);
    check(original.has_value(), "valid completed fixture");
    unsigned calls{};
    const std::filesystem::path destination = "caller-supplied-spelling/../history.json";
    auto created = detail::archive_link_completion_with(completed, destination, "label",
        [&](const auto& p, const LinkFactSnapshot& s) -> Stored {
            ++calls; auto expected = *original; expected.capture_label = "label";
            check(p == destination && s == expected, "path spelling and exact snapshot passed once");
            return LinkFactFileReceipt{876};
        });
    check(created && created->document_bytes == 876 && calls == 1, "original receipt forwarded without readback");
    for (const auto stage : {LinkFactFileStage::input, LinkFactFileStage::ancestor, LinkFactFileStage::open,
                            LinkFactFileStage::write, LinkFactFileStage::flush, LinkFactFileStage::close}) {
        const LinkFactFileError error{LinkFactFileErrorCode::transfer_failed, stage, 112, 1234, true, 6,
            LinkFactSnapshotError{LinkFactSnapshotErrorCode::invalid_document, "original codec"}};
        calls = 0;
        auto failed = detail::archive_link_completion_with(completed, destination, std::nullopt,
            [&](const auto&, const auto&) -> Stored { ++calls; return std::unexpected(error); });
        check(!failed && calls == 1 && std::holds_alternative<LinkFactFileError>(failed.error()), "IO error never retried");
        const auto& actual = std::get<LinkFactFileError>(failed.error());
        check(actual.code == error.code && actual.stage == stage && actual.native_code == 112 &&
            actual.transferred_bytes == 1234 && actual.file_created && actual.close_error == 6u &&
            actual.codec_error && actual.codec_error->code == error.codec_error->code &&
            actual.codec_error->message == "original codec", "all nested native error fields retained");
    }
    auto invalid = completed; invalid.build.result.linked = false;
    calls = 0;
    auto refused = detail::archive_link_completion_with(invalid, destination, std::nullopt,
        [&](const auto&, const auto&) -> Stored { ++calls; return LinkFactFileReceipt{}; });
    auto capture_error = capture_link_fact_snapshot(invalid);
    check(!refused && !capture_error && calls == 0 && std::holds_alternative<LinkFactSnapshotError>(refused.error()),
        "invalid capture prevents writer call");
    check(std::get<LinkFactSnapshotError>(refused.error()).message == capture_error.error().message,
        "original capture diagnostic retained");
    std::string large(LinkFactSnapshotLimits::string_bytes + 1, 'x');
    auto limited = detail::archive_link_completion_with(completed, destination, large,
        [&](const auto&, const auto&) -> Stored { ++calls; return LinkFactFileReceipt{}; });
    check(!limited && calls == 0 && std::get<LinkFactSnapshotError>(limited.error()).code == LinkFactSnapshotErrorCode::limit_exceeded,
        "label budget enforced before writer");
    for (int kind : {0, 1}) {
        calls = 0; bool propagated = false;
        try {
            (void)detail::archive_link_completion_with(completed, destination, std::nullopt,
                [&](const auto&, const auto&) -> Stored { ++calls; if (kind == 0) throw std::bad_alloc{}; throw std::runtime_error("writer"); });
        } catch (const std::bad_alloc&) { propagated = kind == 0; }
        catch (const std::runtime_error& e) { propagated = kind == 1 && std::string_view(e.what()) == "writer"; }
        check(propagated && calls == 1, "exception propagates once without fabricating success");
    }
    const auto after = capture_link_fact_snapshot(completed);
    check(after && *after == *original, "original completion/warnings/observation unchanged");
    check(completed.build.result.process->stdout_text == "original stdout" && completed.build.result.process->stderr_text == "original stderr" &&
        completed.build.result.process->launch_duration.count() == 123 && completed.build.result.process->exit_code == 0,
        "process diagnostics stay with the caller");
    auto reuse = completed; reuse.build.result.linked = false; reuse.build.record.completion = ArtifactCompletion::reused;
    reuse.build.record.cache_state = ArtifactCacheState::reused;
    reuse.observer_invoked = true; reuse.observation.state = StorageFileObservationState::unavailable;
    reuse.observation.issues = {{"old.exe", "busy", 32}};
    auto historical = detail::archive_link_completion_with(reuse, destination, std::nullopt,
        [&](const auto&, const LinkFactSnapshot& s) -> Stored {
            check(!s.linked && s.observation.state == StorageFileObservationState::unavailable &&
                s.observation.issues[0].native_code == 32 && s.warnings.size() == 2 &&
                !s.producer_identity_verified && !s.current_content_verified && !s.complete_producer_inventory && !s.deletion_authorized,
                "reuse and unavailable observation archived only as unverified history");
            return LinkFactFileReceipt{1};
        });
    check(historical.has_value(), "unavailable observation is not a failed build");
}
}
int main() {
    try { run(); std::cout << "completion archive: " << checks << " checks passed (synthetic writer only)\n"; }
    catch (const std::exception& e) { std::cerr << "check " << checks << ": " << e.what() << '\n'; return 1; }
}
