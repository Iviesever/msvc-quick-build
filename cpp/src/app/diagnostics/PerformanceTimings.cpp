#include "PerformanceTimings.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <locale>
#include <memory>
#include <sstream>
#include <streambuf>
#include <string_view>

namespace mqb::app::performance {
namespace {

using Evidence = mqb::performance::EvidenceSnapshot;

constexpr std::array<std::string_view, mqb::performance::wall_kind_count> wall_names{
    "project_setup",
    "artifact_layout",
    "toolchain_discovery",
    "target_validation",
    "reporting",
};

constexpr std::array<std::string_view, mqb::performance::work_kind_count> work_names{
    "compile_inspection",
    "compile_execution",
    "compile_cache_read",
    "compile_cache_write",
    "link_inspection",
    "link_execution",
    "link_resolution",
    "link_cache_read",
    "link_cache_write",
    "archive_inspection",
    "archive_execution",
    "archive_cache_read",
    "archive_cache_write",
    "discovery_cache_read",
    "discovery_cache_write",
    "toolchain_cache_read",
    "toolchain_cache_write",
    "module_scan_execution",
    "filesystem_snapshot_compile",
    "filesystem_snapshot_link",
    "filesystem_snapshot_archive",
    "filesystem_snapshot_discovery",
    "filesystem_snapshot_toolchain",
    "filesystem_snapshot_module_scan",
    "filesystem_snapshot_other",
};

constexpr std::array<std::string_view, mqb::performance::cache_kind_count> cache_names{
    "compile",
    "link",
    "archive",
    "discovery",
    "toolchain",
};

constexpr std::array<std::string_view, mqb::performance::filesystem_kind_count>
filesystem_names{
    "compile",
    "link",
    "archive",
    "discovery",
    "toolchain",
    "module_scan",
    "other",
};

[[nodiscard]] double milliseconds(const std::chrono::nanoseconds duration) noexcept {
    return std::chrono::duration<double, std::milli>{duration}.count();
}

void append_text_phase(
    std::ostringstream& output,
    const std::string_view label,
    const std::chrono::nanoseconds duration) {
    output << "  " << std::left << std::setw(31) << label
           << std::right << std::fixed << std::setprecision(3)
           << std::setw(12) << milliseconds(duration) << " ms\n";
}

template <std::size_t Size>
[[nodiscard]] std::uint64_t sum(
    const std::array<std::uint64_t, Size>& values) noexcept {
    std::uint64_t result = 0;
    for (const std::uint64_t value : values) result += value;
    return result;
}

[[nodiscard]] std::chrono::nanoseconds filesystem_work_total(
    const Evidence& evidence) noexcept {
    using mqb::performance::WorkKind;
    std::chrono::nanoseconds result{};
    for (const WorkKind kind : {
             WorkKind::filesystem_snapshot_compile,
             WorkKind::filesystem_snapshot_link,
             WorkKind::filesystem_snapshot_archive,
             WorkKind::filesystem_snapshot_discovery,
             WorkKind::filesystem_snapshot_toolchain,
             WorkKind::filesystem_snapshot_module_scan,
             WorkKind::filesystem_snapshot_other}) {
        result += evidence.work[static_cast<std::size_t>(kind)];
    }
    return result;
}

void append_json_durations(
    std::ostringstream& output,
    const auto& names,
    const auto& durations) {
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (index != 0) output << ',';
        output << '\"' << names[index] << "\":"
               << milliseconds(durations[index]);
    }
}

void append_json_cache_breakdown(
    std::ostringstream& output,
    const Evidence& evidence) {
    for (std::size_t index = 0; index < cache_names.size(); ++index) {
        if (index != 0) output << ',';
        output << '\"' << cache_names[index] << "\":{"
               << "\"files_opened\":" << evidence.cache_files_opened[index] << ','
               << "\"bytes_read\":" << evidence.cache_bytes_read[index] << ','
               << "\"files_written\":" << evidence.cache_files_written[index] << ','
               << "\"bytes_written\":" << evidence.cache_bytes_written[index]
               << '}';
    }
}

void append_json_filesystem_breakdown(
    std::ostringstream& output,
    const Evidence& evidence) {
    for (std::size_t index = 0; index < filesystem_names.size(); ++index) {
        if (index != 0) output << ',';
        output << '\"' << filesystem_names[index] << "\":{"
               << "\"snapshot_requests\":"
               << evidence.filesystem_snapshot_requests[index] << ','
               << "\"unique_paths_probed\":"
               << evidence.unique_filesystem_paths_probed[index] << ','
               << "\"snapshot_evidence_reuses\":"
               << evidence.snapshot_evidence_reuses[index]
               << '}';
    }
}

class CountingStreambuf final : public std::streambuf {
public:
    CountingStreambuf(
        std::streambuf* destination,
        mqb::performance::Collector& collector) noexcept
        : destination_(destination), collector_(&collector) {}

protected:
    int_type overflow(const int_type character) override {
        if (traits_type::eq_int_type(character, traits_type::eof())) {
            return traits_type::not_eof(character);
        }
        const auto started = mqb::performance::EvidenceClock::now();
        const int_type result = destination_->sputc(traits_type::to_char_type(character));
        collector_->add_wall(
            mqb::performance::WallKind::reporting,
            mqb::performance::EvidenceClock::now() - started);
        if (!traits_type::eq_int_type(result, traits_type::eof())) {
            collector_->record_output(
                1,
                traits_type::to_char_type(character) == '\n' ? 1 : 0);
        }
        return result;
    }

    std::streamsize xsputn(
        const char_type* text,
        const std::streamsize count) override {
        const auto started = mqb::performance::EvidenceClock::now();
        const std::streamsize written = destination_->sputn(text, count);
        collector_->add_wall(
            mqb::performance::WallKind::reporting,
            mqb::performance::EvidenceClock::now() - started);
        if (written > 0) {
            const auto length = static_cast<std::size_t>(written);
            const auto lines = static_cast<std::uint64_t>(
                std::count(text, text + length, '\n'));
            collector_->record_output(
                static_cast<std::uint64_t>(length),
                lines);
        }
        return written;
    }

    int sync() override {
        const auto started = mqb::performance::EvidenceClock::now();
        const int result = destination_->pubsync();
        collector_->add_wall(
            mqb::performance::WallKind::reporting,
            mqb::performance::EvidenceClock::now() - started);
        return result;
    }

private:
    std::streambuf* destination_{};
    mqb::performance::Collector* collector_{};
};

} // namespace

struct Session::OutputObservers {
    explicit OutputObservers(mqb::performance::Collector& collector)
        : cout_original(std::cout.rdbuf()),
          cerr_original(std::cerr.rdbuf()),
          clog_original(std::clog.rdbuf()),
          cout_observer(std::make_unique<CountingStreambuf>(cout_original, collector)),
          cerr_observer(std::make_unique<CountingStreambuf>(cerr_original, collector)),
          clog_observer(std::make_unique<CountingStreambuf>(clog_original, collector)) {
        std::cout.rdbuf(cout_observer.get());
        std::cerr.rdbuf(cerr_observer.get());
        std::clog.rdbuf(clog_observer.get());
    }

    ~OutputObservers() noexcept {
        try {
            std::cout.flush();
            std::cerr.flush();
            std::clog.flush();
        } catch (...) {
        }
        std::cout.rdbuf(cout_original);
        std::cerr.rdbuf(cerr_original);
        std::clog.rdbuf(clog_original);
    }

    std::streambuf* cout_original{};
    std::streambuf* cerr_original{};
    std::streambuf* clog_original{};
    std::unique_ptr<CountingStreambuf> cout_observer;
    std::unique_ptr<CountingStreambuf> cerr_observer;
    std::unique_ptr<CountingStreambuf> clog_observer;
};

std::string render(const Snapshot& snapshot, const Format format) {
    if (format == Format::disabled) return {};

    std::ostringstream output;
    output.imbue(std::locale::classic());

    if (format == Format::json) {
        const Evidence& evidence = snapshot.evidence;
        output << std::fixed << std::setprecision(3)
               << "{\"type\":\"mqb.timings\",\"schema_version\":2,\"unit\":\"ms\",\"phases\":{"
               << "\"discovery\":" << milliseconds(snapshot.discovery) << ','
               << "\"dependency_scan\":" << milliseconds(snapshot.target.dependency_scan) << ','
               << "\"compile_queue\":" << milliseconds(snapshot.target.compile_queue) << ','
               << "\"compile\":" << milliseconds(snapshot.target.compile) << ','
               << "\"link\":" << milliseconds(snapshot.target.link) << ','
               << "\"archive\":" << milliseconds(snapshot.target.archive) << ','
               << "\"run_startup\":" << milliseconds(snapshot.run_startup) << ','
               << "\"total\":" << milliseconds(snapshot.total)
               << "},\"cache\":{"
               << "\"compile\":{\"hits\":" << snapshot.cache.compile_hits
               << ",\"misses\":" << snapshot.cache.compile_misses << "},"
               << "\"link\":{\"hits\":" << snapshot.cache.link_hits
               << ",\"misses\":" << snapshot.cache.link_misses << "},"
               << "\"archive\":{\"hits\":" << snapshot.cache.archive_hits
               << ",\"misses\":" << snapshot.cache.archive_misses << "}},"
               << "\"attribution\":{\"wall\":{"
               ;
        append_json_durations(output, wall_names, evidence.wall);
        output << "},\"work\":{"
               << "\"filesystem_snapshot\":"
               << milliseconds(filesystem_work_total(evidence));
        if (!work_names.empty()) output << ',';
        append_json_durations(output, work_names, evidence.work);
        output << "}},\"counters\":{"
               << "\"cache_files_opened\":" << sum(evidence.cache_files_opened) << ','
               << "\"cache_bytes_read\":" << sum(evidence.cache_bytes_read) << ','
               << "\"cache_files_written\":" << sum(evidence.cache_files_written) << ','
               << "\"cache_bytes_written\":" << sum(evidence.cache_bytes_written) << ','
               << "\"filesystem_snapshot_requests\":"
               << sum(evidence.filesystem_snapshot_requests) << ','
               << "\"unique_filesystem_paths_probed\":"
               << evidence.unique_filesystem_paths_total << ','
               << "\"snapshot_evidence_reuses\":"
               << sum(evidence.snapshot_evidence_reuses) << ','
               << "\"background_threads_created\":"
               << evidence.background_threads_created << ','
               << "\"cl_processes_launched\":"
               << evidence.processes_launched[
                      static_cast<std::size_t>(mqb::performance::ProcessKind::compiler)] << ','
               << "\"link_processes_launched\":"
               << evidence.processes_launched[
                      static_cast<std::size_t>(mqb::performance::ProcessKind::linker)] << ','
               << "\"lib_processes_launched\":"
               << evidence.processes_launched[
                      static_cast<std::size_t>(mqb::performance::ProcessKind::librarian)] << ','
               << "\"output_lines_emitted\":" << evidence.output_lines_emitted << ','
               << "\"output_bytes_emitted\":" << evidence.output_bytes_emitted
               << "},\"counter_breakdown\":{\"cache\":{"
               ;
        append_json_cache_breakdown(output, evidence);
        output << "},\"filesystem\":{"
               ;
        append_json_filesystem_breakdown(output, evidence);
        output << "},\"process\":{"
               << "\"compiler\":"
               << evidence.processes_launched[
                      static_cast<std::size_t>(mqb::performance::ProcessKind::compiler)] << ','
               << "\"linker\":"
               << evidence.processes_launched[
                      static_cast<std::size_t>(mqb::performance::ProcessKind::linker)] << ','
               << "\"librarian\":"
               << evidence.processes_launched[
                      static_cast<std::size_t>(mqb::performance::ProcessKind::librarian)]
               << "}}}\n";
        return output.str();
    }

    output << "[timings]\n";
    append_text_phase(output, "discovery", snapshot.discovery);
    append_text_phase(output, "dependency-scan", snapshot.target.dependency_scan);
    append_text_phase(output, "compile-queue", snapshot.target.compile_queue);
    append_text_phase(output, "compile", snapshot.target.compile);
    append_text_phase(output, "link", snapshot.target.link);
    append_text_phase(output, "archive", snapshot.target.archive);
    append_text_phase(output, "run-startup", snapshot.run_startup);
    append_text_phase(output, "total", snapshot.total);
    output << "  cache                         compile "
           << snapshot.cache.compile_hits << " hit / "
           << snapshot.cache.compile_misses << " miss; link "
           << snapshot.cache.link_hits << " hit / " << snapshot.cache.link_misses
           << " miss; archive " << snapshot.cache.archive_hits << " hit / "
           << snapshot.cache.archive_misses << " miss\n";

    output << "[attribution wall]\n";
    for (std::size_t index = 0; index < wall_names.size(); ++index) {
        append_text_phase(output, wall_names[index], snapshot.evidence.wall[index]);
    }
    output << "[attribution cumulative work]\n";
    append_text_phase(
        output,
        "filesystem_snapshot",
        filesystem_work_total(snapshot.evidence));
    for (std::size_t index = 0; index < work_names.size(); ++index) {
        append_text_phase(output, work_names[index], snapshot.evidence.work[index]);
    }
    output << "[counters]\n"
           << "  cache files opened:           "
           << sum(snapshot.evidence.cache_files_opened) << '\n'
           << "  cache bytes read:             "
           << sum(snapshot.evidence.cache_bytes_read) << '\n'
           << "  filesystem snapshot requests: "
           << sum(snapshot.evidence.filesystem_snapshot_requests) << '\n'
           << "  unique filesystem paths:      "
           << snapshot.evidence.unique_filesystem_paths_total << '\n'
           << "  snapshot evidence reuses:     "
           << sum(snapshot.evidence.snapshot_evidence_reuses) << '\n'
           << "  background threads created:   "
           << snapshot.evidence.background_threads_created << '\n'
           << "  cl/link/lib launches:         "
           << snapshot.evidence.processes_launched[0] << '/'
           << snapshot.evidence.processes_launched[1] << '/'
           << snapshot.evidence.processes_launched[2] << '\n'
           << "  output lines/bytes:           "
           << snapshot.evidence.output_lines_emitted << '/'
           << snapshot.evidence.output_bytes_emitted << '\n';
    return output.str();
}

Session::Session(
    const Format format,
    const Clock::time_point application_started) noexcept
    : format_(format), application_started_(application_started) {
    if (format_ == Format::disabled) return;
    try {
        activation_ = std::make_unique<mqb::performance::Activation>(evidence_);
        output_observers_ = std::make_unique<OutputObservers>(evidence_);
    } catch (...) {
        output_observers_.reset();
        activation_.reset();
        format_ = Format::disabled;
    }
}

Session::~Session() noexcept {
    if (format_ == Format::disabled) return;

    // Restore streams and deactivate observation before rendering. The timing
    // record itself is therefore excluded from reporting and output counters.
    output_observers_.reset();
    activation_.reset();
    try {
        std::clog << render(snapshot(), format_) << std::flush;
    } catch (...) {
        // Timing diagnostics must never change build success or failure.
    }
}

void Session::add_discovery(const Clock::duration duration) noexcept {
    accumulated_.discovery += std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
}

void Session::add_target(const orchestration::TargetTimings& timings) noexcept {
    accumulated_.target.dependency_scan += timings.dependency_scan;
    accumulated_.target.compile_queue += timings.compile_queue;
    accumulated_.target.compile += timings.compile;
    accumulated_.target.link += timings.link;
    accumulated_.target.archive += timings.archive;
}

void Session::record_compile(const bool compiled) noexcept {
    if (compiled) ++accumulated_.cache.compile_misses;
    else ++accumulated_.cache.compile_hits;
}

void Session::record_link(const bool linked) noexcept {
    if (linked) ++accumulated_.cache.link_misses;
    else ++accumulated_.cache.link_hits;
}

void Session::record_archive(const bool archived) noexcept {
    if (archived) ++accumulated_.cache.archive_misses;
    else ++accumulated_.cache.archive_hits;
}

void Session::record_run_startup(const std::chrono::nanoseconds duration) noexcept {
    accumulated_.run_startup += duration;
}

Snapshot Session::snapshot(const Clock::time_point now) const noexcept {
    Snapshot result = accumulated_;
    result.total = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now - application_started_);
    result.evidence = evidence_.snapshot();
    return result;
}

} // namespace mqb::app::performance
