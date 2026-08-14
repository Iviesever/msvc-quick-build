#include "PerformanceTimings.hpp"

#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>

namespace mqb::app::performance {
namespace {

[[nodiscard]] double milliseconds(const std::chrono::nanoseconds duration) noexcept {
    return std::chrono::duration<double, std::milli>{duration}.count();
}

void append_text_phase(
    std::ostringstream& output,
    const char* const label,
    const std::chrono::nanoseconds duration) {
    output << "  " << std::left << std::setw(17) << label
           << std::right << std::fixed << std::setprecision(3)
           << std::setw(12) << milliseconds(duration) << " ms\n";
}

} // namespace

std::string render(const Snapshot& snapshot, const Format format) {
    if (format == Format::disabled) {
        return {};
    }

    std::ostringstream output;
    output.imbue(std::locale::classic());

    if (format == Format::json) {
        output << std::fixed << std::setprecision(3)
               << "{\"type\":\"mqb.timings\",\"schema_version\":1,\"unit\":\"ms\",\"phases\":{"
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
               << ",\"misses\":" << snapshot.cache.archive_misses << "}}}\n";
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
    output << "  cache             compile " << snapshot.cache.compile_hits << " hit / "
           << snapshot.cache.compile_misses << " miss; link "
           << snapshot.cache.link_hits << " hit / " << snapshot.cache.link_misses
           << " miss; archive " << snapshot.cache.archive_hits << " hit / "
           << snapshot.cache.archive_misses << " miss\n";
    return output.str();
}

Session::~Session() noexcept {
    if (format_ == Format::disabled) {
        return;
    }
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
    return result;
}

} // namespace mqb::app::performance
