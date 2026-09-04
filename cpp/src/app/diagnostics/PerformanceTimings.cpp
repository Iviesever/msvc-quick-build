#include "PerformanceTimings.hpp"

#include <algorithm>
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

[[nodiscard]] double milliseconds(const std::chrono::nanoseconds duration) noexcept {
    return std::chrono::duration<double, std::milli>{duration}.count();
}

void append_text_phase(
    std::ostringstream& output,
    const char* const label,
    const std::chrono::nanoseconds duration) {
    output << "  " << std::left << std::setw(24) << label
           << std::right << std::fixed << std::setprecision(3)
           << std::setw(12) << milliseconds(duration) << " ms\n";
}

void append_text_counter(
    std::ostringstream& output,
    const char* const label,
    const std::uint64_t value) {
    output << "  " << std::left << std::setw(32) << label
           << std::right << value << '\n';
}

[[nodiscard]] std::uint64_t newline_count(
    const char* const data,
    const std::streamsize size) noexcept {
    if (data == nullptr || size <= 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(
        std::count(data, data + size, '\n'));
}

} // namespace

namespace detail {
namespace {

class CountingStreambuf final : public std::streambuf {
public:
    explicit CountingStreambuf(std::streambuf* destination) noexcept
        : destination_(destination) {}

protected:
    int_type overflow(const int_type character) override {
        if (traits_type::eq_int_type(character, traits_type::eof())) {
            return traits_type::not_eof(character);
        }

        mqb::diagnostics::ScopedPerformancePhase reporting{
            mqb::diagnostics::PerformancePhase::reporting};
        const int_type written = destination_->sputc(
            traits_type::to_char_type(character));
        if (!traits_type::eq_int_type(written, traits_type::eof())) {
            const char value = traits_type::to_char_type(character);
            mqb::diagnostics::record_output(1, value == '\n' ? 1 : 0);
        }
        return written;
    }

    std::streamsize xsputn(
        const char* const data,
        const std::streamsize size) override {
        if (size <= 0) {
            return 0;
        }

        mqb::diagnostics::ScopedPerformancePhase reporting{
            mqb::diagnostics::PerformancePhase::reporting};
        const std::streamsize written = destination_->sputn(data, size);
        if (written > 0) {
            mqb::diagnostics::record_output(
                static_cast<std::uint64_t>(written),
                newline_count(data, written));
        }
        return written;
    }

    int sync() override {
        mqb::diagnostics::ScopedPerformancePhase reporting{
            mqb::diagnostics::PerformancePhase::reporting};
        return destination_->pubsync();
    }

private:
    std::streambuf* destination_{};
};

} // namespace

class OutputObserver {
public:
    OutputObserver() noexcept
        : cout_original_(std::cout.rdbuf()),
          cerr_original_(std::cerr.rdbuf()),
          clog_original_(std::clog.rdbuf()),
          cout_buffer_(cout_original_),
          cerr_buffer_(cerr_original_),
          clog_buffer_(clog_original_) {
        std::cout.rdbuf(&cout_buffer_);
        std::cerr.rdbuf(&cerr_buffer_);
        std::clog.rdbuf(&clog_buffer_);
    }

    ~OutputObserver() noexcept {
        std::cout.rdbuf(cout_original_);
        std::cerr.rdbuf(cerr_original_);
        std::clog.rdbuf(clog_original_);
    }

    OutputObserver(const OutputObserver&) = delete;
    OutputObserver& operator=(const OutputObserver&) = delete;

private:
    std::streambuf* cout_original_{};
    std::streambuf* cerr_original_{};
    std::streambuf* clog_original_{};
    CountingStreambuf cout_buffer_;
    CountingStreambuf cerr_buffer_;
    CountingStreambuf clog_buffer_;
};

} // namespace detail

std::string render(const Snapshot& snapshot, const Format format) {
    if (format == Format::disabled) {
        return {};
    }

    std::ostringstream output;
    output.imbue(std::locale::classic());

    const auto& phases = snapshot.evidence.phases;
    const auto& counters = snapshot.evidence.counters;

    if (format == Format::json) {
        output << std::fixed << std::setprecision(3)
               << "{\"type\":\"mqb.timings\",\"schema_version\":2,\"unit\":\"ms\",\"phases\":{"
               // Schema-v1 fields retain their original names and semantics.
               << "\"discovery\":" << milliseconds(snapshot.discovery) << ','
               << "\"dependency_scan\":" << milliseconds(snapshot.target.dependency_scan) << ','
               << "\"compile_queue\":" << milliseconds(snapshot.target.compile_queue) << ','
               << "\"compile\":" << milliseconds(snapshot.target.compile) << ','
               << "\"link\":" << milliseconds(snapshot.target.link) << ','
               << "\"archive\":" << milliseconds(snapshot.target.archive) << ','
               << "\"run_startup\":" << milliseconds(snapshot.run_startup) << ','
               << "\"total\":" << milliseconds(snapshot.total) << ','
               // Schema-v2 attribution fields are additive evidence. Parallel
               // calls use elapsed-time union per phase, not summed CPU time.
               << "\"project_setup\":" << milliseconds(phases.project_setup) << ','
               << "\"artifact_layout\":" << milliseconds(phases.artifact_layout) << ','
               << "\"toolchain_discovery\":" << milliseconds(phases.toolchain_discovery) << ','
               << "\"target_validation\":" << milliseconds(phases.target_validation) << ','
               << "\"compile_inspection\":" << milliseconds(phases.compile_inspection) << ','
               << "\"compile_execution\":" << milliseconds(phases.compile_execution) << ','
               << "\"compile_cache_read\":" << milliseconds(phases.compile_cache_read) << ','
               << "\"compile_cache_write\":" << milliseconds(phases.compile_cache_write) << ','
               << "\"link_inspection\":" << milliseconds(phases.link_inspection) << ','
               << "\"link_execution\":" << milliseconds(phases.link_execution) << ','
               << "\"link_resolution\":" << milliseconds(phases.link_resolution) << ','
               << "\"archive_inspection\":" << milliseconds(phases.archive_inspection) << ','
               << "\"archive_execution\":" << milliseconds(phases.archive_execution) << ','
               << "\"reporting\":" << milliseconds(phases.reporting)
               << "},\"cache\":{"
               << "\"compile\":{\"hits\":" << snapshot.cache.compile_hits
               << ",\"misses\":" << snapshot.cache.compile_misses << "},"
               << "\"link\":{\"hits\":" << snapshot.cache.link_hits
               << ",\"misses\":" << snapshot.cache.link_misses << "},"
               << "\"archive\":{\"hits\":" << snapshot.cache.archive_hits
               << ",\"misses\":" << snapshot.cache.archive_misses << "}},"
               << "\"counters\":{"
               << "\"cache_files_opened\":" << counters.cache_files_opened << ','
               << "\"cache_bytes_read\":" << counters.cache_bytes_read << ','
               << "\"filesystem_snapshot_requests\":"
               << counters.filesystem_snapshot_requests << ','
               << "\"unique_filesystem_paths_probed\":"
               << counters.unique_filesystem_paths_probed << ','
               << "\"snapshot_evidence_reuses\":" << counters.snapshot_evidence_reuses << ','
               << "\"background_threads_created\":" << counters.background_threads_created << ','
               << "\"cl_processes_launched\":" << counters.cl_processes_launched << ','
               << "\"link_processes_launched\":" << counters.link_processes_launched << ','
               << "\"lib_processes_launched\":" << counters.lib_processes_launched << ','
               << "\"output_lines_emitted\":" << counters.output_lines_emitted << ','
               << "\"output_bytes_emitted\":" << counters.output_bytes_emitted
               << "}}\n";
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
    append_text_phase(output, "project-setup", phases.project_setup);
    append_text_phase(output, "artifact-layout", phases.artifact_layout);
    append_text_phase(output, "toolchain-discovery", phases.toolchain_discovery);
    append_text_phase(output, "target-validation", phases.target_validation);
    append_text_phase(output, "compile-inspection", phases.compile_inspection);
    append_text_phase(output, "compile-execution", phases.compile_execution);
    append_text_phase(output, "compile-cache-read", phases.compile_cache_read);
    append_text_phase(output, "compile-cache-write", phases.compile_cache_write);
    append_text_phase(output, "link-inspection", phases.link_inspection);
    append_text_phase(output, "link-execution", phases.link_execution);
    append_text_phase(output, "link-resolution", phases.link_resolution);
    append_text_phase(output, "archive-inspection", phases.archive_inspection);
    append_text_phase(output, "archive-execution", phases.archive_execution);
    append_text_phase(output, "reporting", phases.reporting);
    append_text_phase(output, "total", snapshot.total);
    output << "  cache                    compile " << snapshot.cache.compile_hits << " hit / "
           << snapshot.cache.compile_misses << " miss; link "
           << snapshot.cache.link_hits << " hit / " << snapshot.cache.link_misses
           << " miss; archive " << snapshot.cache.archive_hits << " hit / "
           << snapshot.cache.archive_misses << " miss\n";
    output << "[evidence]\n";
    append_text_counter(output, "cache-files-opened", counters.cache_files_opened);
    append_text_counter(output, "cache-bytes-read", counters.cache_bytes_read);
    append_text_counter(output, "filesystem-snapshot-requests", counters.filesystem_snapshot_requests);
    append_text_counter(output, "unique-filesystem-paths-probed", counters.unique_filesystem_paths_probed);
    append_text_counter(output, "snapshot-evidence-reuses", counters.snapshot_evidence_reuses);
    append_text_counter(output, "background-threads-created", counters.background_threads_created);
    append_text_counter(output, "cl-processes-launched", counters.cl_processes_launched);
    append_text_counter(output, "link-processes-launched", counters.link_processes_launched);
    append_text_counter(output, "lib-processes-launched", counters.lib_processes_launched);
    append_text_counter(output, "output-lines-emitted", counters.output_lines_emitted);
    append_text_counter(output, "output-bytes-emitted", counters.output_bytes_emitted);
    return output.str();
}

Session::Session(
    const Format format,
    const Clock::time_point application_started) noexcept
    : format_(format), application_started_(application_started) {
    if (format_ == Format::disabled) {
        return;
    }
    try {
        evidence_session_ =
            std::make_unique<mqb::diagnostics::PerformanceEvidenceSession>();
        if (evidence_session_->registered()) {
            output_observer_ = std::make_unique<detail::OutputObserver>();
        }
    } catch (...) {
        output_observer_.reset();
        evidence_session_.reset();
    }
}

Session::~Session() noexcept {
    if (format_ == Format::disabled) {
        return;
    }

    // Restore the real streams before taking the final snapshot. The timing
    // record itself is intentionally excluded from reporting/output evidence.
    output_observer_.reset();
    try {
        std::clog << render(snapshot(), format_) << std::flush;
    } catch (...) {
        // Timing diagnostics must never change build success or failure.
    }
    evidence_session_.reset();
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
    if (compiled) {
        ++accumulated_.cache.compile_misses;
    } else {
        ++accumulated_.cache.compile_hits;
    }
}

void Session::record_link(const bool linked) noexcept {
    if (linked) {
        ++accumulated_.cache.link_misses;
    } else {
        ++accumulated_.cache.link_hits;
    }
}

void Session::record_archive(const bool archived) noexcept {
    if (archived) {
        ++accumulated_.cache.archive_misses;
    } else {
        ++accumulated_.cache.archive_hits;
    }
}

void Session::record_run_startup(const std::chrono::nanoseconds duration) noexcept {
    accumulated_.run_startup += duration;
}

Snapshot Session::snapshot(const Clock::time_point now) const noexcept {
    Snapshot result = accumulated_;
    result.total = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now - application_started_);
    if (evidence_session_) {
        result.evidence = evidence_session_->snapshot();
    }
    return result;
}

} // namespace mqb::app::performance
