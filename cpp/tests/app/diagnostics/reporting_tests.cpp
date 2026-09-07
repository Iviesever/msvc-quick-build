#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Diagnostics.hpp"
#include "PerformanceTimings.hpp"
#include "ReportBuffer.hpp"

namespace {
namespace diagnostics = mqb::app::diagnostics;
namespace performance = mqb::app::performance;
namespace fs = std::filesystem;
int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

struct Sink final : std::streambuf {
    std::string text;
    std::vector<std::size_t> writes;
    std::vector<std::pair<char, std::string>>* events{};
    char channel{'o'};
    bool short_write{};
    bool throw_write{};

    std::streamsize xsputn(const char* data, const std::streamsize count) override {
        writes.push_back(static_cast<std::size_t>(count));
        if (throw_write) throw std::runtime_error{"injected output failure"};
        const auto written = short_write ? std::min(count, std::streamsize{3}) : count;
        text.append(data, static_cast<std::size_t>(written));
        if (events) events->emplace_back(channel, std::string{data, static_cast<std::size_t>(written)});
        return written;
    }
    int_type overflow(const int_type value) override {
        if (traits_type::eq_int_type(value, traits_type::eof())) return traits_type::not_eof(value);
        const char ch = traits_type::to_char_type(value);
        return xsputn(&ch, 1) == 1 ? value : traits_type::eof();
    }
};

struct Capture {
    Sink out;
    Sink err;
    Sink log;
    std::vector<std::pair<char, std::string>> events;
    std::streambuf* cout_original{std::cout.rdbuf(&out)};
    std::streambuf* cerr_original{std::cerr.rdbuf(&err)};
    std::streambuf* clog_original{std::clog.rdbuf(&log)};

    Capture() {
        out.events = &events;
        err.events = &events;
        err.channel = 'e';
    }
    ~Capture() {
        std::cout.rdbuf(cout_original);
        std::cerr.rdbuf(cerr_original);
        std::clog.rdbuf(clog_original);
    }
};

void test_bounded_buffer() {
    Sink sink;
    std::ostream output{&sink};
    const std::string bytes(diagnostics::detail::ReportBuffer::capacity * 3u + 7u, 'x');
    {
        diagnostics::detail::ReportBuffer buffer{output};
        buffer.append(std::string_view{});
        buffer.append(bytes);
        buffer.flush();
        buffer.flush();
    }
    expect(sink.text == bytes, "bounded output must preserve every byte over multiple chunks");
    expect(sink.writes.size() == 4, "large output must be batched, not written character by character");
    expect(std::all_of(sink.writes.begin(), sink.writes.end(), [](const auto count) {
        return count <= diagnostics::detail::ReportBuffer::capacity;
    }), "every batched write must respect the fixed memory/write bound");

    Sink short_sink;
    short_sink.short_write = true;
    std::ostream short_output{&short_sink};
    {
        diagnostics::detail::ReportBuffer buffer{short_output};
        buffer.append("abcdef");
        buffer.flush();
        buffer.append("must not retry");
    }
    expect(short_output.bad() && short_sink.text == "abc" && short_sink.writes.size() == 1,
           "a short write must keep badbit without retrying or duplicating its prefix");

    Sink throwing_sink;
    throwing_sink.throw_write = true;
    std::ostream throwing_output{&throwing_sink};
    throwing_output.exceptions(std::ios::badbit);
    bool caught = false;
    try {
        diagnostics::detail::ReportBuffer buffer{throwing_output};
        buffer.append("failure");
        buffer.flush();
    } catch (...) { caught = true; }
    expect(caught && throwing_output.bad() && throwing_sink.writes.size() == 1,
           "explicit flush must propagate configured exceptions without destructor retries");
}

void test_forwarded_bytes() {
    mqb::process::ProcessResult process;
    process.stdout_text = std::string{"a\r\nb\rc\n\0", 8} + "\xc3\xa9";
    process.stderr_text = "error\r\nnext\r";
    std::string out;
    std::string err;
    {
        Capture capture;
        diagnostics::print_process_output(process);
        out = capture.out.text;
        err = capture.err.text;
    }
    expect(out == std::string{"a\nb\rc\n\0", 7} + "\xc3\xa9\n",
           "forwarding must preserve UTF-8/NUL/standalone CR, normalize CRLF and add a missing newline");
    expect(err == "error\nnext\r\n", "stderr must retain the same normalization and newline policy");

    process.stdout_text = std::string(diagnostics::detail::ReportBuffer::capacity * 2u - 1u, 'x') + "\r\nend\n";
    process.stderr_text.clear();
    std::size_t writes = 0;
    {
        Capture capture;
        diagnostics::print_process_output(process);
        out = capture.out.text;
        err = capture.err.text;
        writes = capture.out.writes.size();
    }
    expect(out == std::string(diagnostics::detail::ReportBuffer::capacity * 2u - 1u, 'x') + "\nend\n"
               && err.empty() && writes == 3,
           "CRLF at a chunk boundary must remain exact and bounded");
    {
        Capture capture;
        diagnostics::print_process_output({});
        out = capture.out.text;
        err = capture.err.text;
        writes = capture.out.writes.size() + capture.err.writes.size();
    }
    expect(out.empty() && err.empty() && writes == 0, "empty process output must not add blank lines");
}

void test_target_reports() {
    const fs::path root{"project"};
    const fs::path executable = root / ".mqb/bin/app.exe";
    std::vector<mqb::orchestration::TargetCompileResult> compiles(129);
    for (std::size_t index = 0; index < compiles.size(); ++index) {
        compiles[index].source = root / "src" / ("unit_" + std::to_string(index) + ".cpp");
    }
    mqb::orchestration::IncrementalLinkResult link;
    std::string out;
    std::string err;
    std::string timing_text;
    std::size_t writes = 0;
    performance::Snapshot snapshot;
    {
        Capture capture;
        {
            performance::Session session{performance::Format::json};
            diagnostics::print_target_report(compiles, link, executable, root, false);
            snapshot = session.snapshot();
        }
        out = capture.out.text;
        err = capture.err.text;
        timing_text = capture.log.text;
        writes = capture.out.writes.size();
    }
    expect(out == "[up-to-date] 129 translation units\n[up-to-date] app.exe\noutput: project/.mqb/bin/app.exe\n",
           "default no-op must have one exact reused-TU summary plus artifact and output lines");
    expect(err.empty() && writes == 1, "a normal no-op report must reach stdout in one batch");
    expect(snapshot.evidence.output_lines_emitted == 3
               && snapshot.evidence.output_bytes_emitted == out.size(),
           "output counters must count emitted report bytes/lines, excluding timing JSON");
    expect(timing_text.find("\"target_reporting\":") != std::string::npos
               && timing_text.find("\"reporting\":") != std::string::npos
               && timing_text.find("\"schema_version\":2") != std::string::npos,
           "inclusive target reporting must be additive metadata, not a redefinition of legacy reporting");

    {
        Capture capture;
        diagnostics::print_target_report(compiles, link, executable, root, true);
        out = capture.out.text;
        writes = capture.out.writes.size();
    }
    std::string expected;
    for (std::size_t index = 0; index < compiles.size(); ++index) {
        expected += "[up-to-date] src/unit_" + std::to_string(index) + ".cpp\n";
    }
    expected += "[up-to-date] app.exe\noutput: project/.mqb/bin/app.exe\n";
    expect(out == expected && writes == 1, "verbose must preserve exact per-TU ordering while batching");

    compiles.resize(2);
    compiles[0].result.warnings.push_back({.path = "cache.bin", .message = "retained cache warning"});
    compiles[1].result.compiled = true;
    compiles[1].result.validation.reasons = {mqb::BuildReason::source_changed};
    compiles[1].result.process = mqb::process::ProcessResult{
        .stdout_text = "compiler stdout\r\n", .stderr_text = "compiler diagnostic"};
    link.linked = true;
    link.validation.reasons = {mqb::BuildReason::explicit_rebuild};
    link.process = mqb::process::ProcessResult{.stdout_text = "linker stdout\n"};
    {
        Capture capture;
        diagnostics::print_target_report(compiles, link, executable, root, false);
        out = capture.out.text;
        err = capture.err.text;
    }
    expect(out == "[compile] src/unit_1.cpp [source changed]\ncompiler stdout\n"
                  "[up-to-date] 1 translation unit\n[link] app.exe [explicit rebuild]\n"
                  "linker stdout\noutput: project/.mqb/bin/app.exe\n",
           "mixed reports must show rebuilt TU/reasons/process output and count only reused TUs");
    expect(err == "warning: retained cache warning: cache.bin\ncompiler diagnostic\n",
           "summary mode must never suppress cache warnings or compiler diagnostics");

    // Ensure buffered earlier stdout is emitted before a warning on the next TU.
    compiles[0].result.warnings.clear();
    compiles[1].result.warnings.push_back({.message = "second TU warning"});
    std::vector<std::pair<char, std::string>> events;
    {
        Capture capture;
        diagnostics::print_target_report(compiles, link, executable, root, true);
        events = capture.events;
    }
    expect(!events.empty() && events.front().first == 'o'
               && events.front().second == "[up-to-date] src/unit_0.cpp\n",
           "batching must flush preceding progress before crossing to stderr");

    mqb::orchestration::IncrementalArchiveResult archive;
    {
        Capture capture;
        diagnostics::print_static_target_report(compiles, archive, "project/.mqb/bin/math.lib", false);
        out = capture.out.text;
        err = capture.err.text;
    }
    expect(out.find("[compile] unit_1.cpp") != std::string::npos
               && out.find("[up-to-date] 1 translation unit\n[up-to-date] math.lib") != std::string::npos,
           "static targets must share summary policy and retain legacy basename labels");
    expect(err.find("warning: second TU warning") != std::string::npos,
           "static targets must retain compile warnings as well as archive warnings");

    compiles.clear();
    {
        Capture capture;
        diagnostics::print_static_target_report(compiles, archive, "math.lib", false);
        out = capture.out.text;
    }
    expect(out == "[up-to-date] math.lib\noutput: math.lib\n",
           "an empty compile list must not invent a reused-TU count");

    compiles.resize(1);
    compiles[0].source = "external/unit.cpp";
    {
        Capture capture;
        diagnostics::print_target_report(compiles, {}, executable, root, true);
        out = capture.out.text;
    }
    expect(out.find("[up-to-date] external/unit.cpp\n") == 0,
           "outside-root labels must not silently gain unsafe parent-relative spellings");
}

void test_failure_reports() {
    mqb::orchestration::IncrementalCompileError compile;
    compile.message = "compile stage failed";
    compile.compile_error.emplace();
    compile.compile_error->message = "executor failed";
    compile.compile_error->compiler_error.emplace();
    compile.compile_error->compiler_error->message = "compiler failed";
    compile.compile_error->compiler_error->process_result = mqb::process::ProcessResult{
        .stdout_text = "error C1234: failed source\r\n", .stderr_text = "tool detail"};
    mqb::orchestration::IncrementalTargetError ordinary;
    ordinary.message = "target failed";
    ordinary.compile_error = compile;
    mqb::orchestration::IncrementalStaticTargetError archive;
    archive.message = "target failed";
    archive.compile_error = compile;
    std::string ordinary_out;
    std::string ordinary_err;
    std::string static_out;
    std::string static_err;
    {
        Capture capture;
        diagnostics::print_target_failure(ordinary);
        ordinary_out = capture.out.text;
        ordinary_err = capture.err.text;
    }
    {
        Capture capture;
        diagnostics::print_static_target_failure(archive);
        static_out = capture.out.text;
        static_err = capture.err.text;
    }
    expect(ordinary_out == "error C1234: failed source\n"
               && ordinary_err == "error: target failed\nerror: compile stage failed\n"
                                  "  executor failed\n  compiler failed\ntool detail\n",
           "failure reports must retain nested compiler output with unchanged normalization");
    expect(static_out == ordinary_out && static_err == ordinary_err,
           "static target failures must expand the same compile diagnostic chain");
}

} // namespace

int main() {
    test_bounded_buffer();
    test_forwarded_bytes();
    test_target_reports();
    test_failure_reports();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_reporting_tests passed\n";
    return 0;
}
