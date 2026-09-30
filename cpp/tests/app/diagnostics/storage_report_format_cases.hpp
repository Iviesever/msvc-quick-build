#pragma once

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>

#include "StorageReport.hpp"
#include "mqb/json/Json.hpp"

// Pure report tests: no filesystem observation, process launch or global streambuf replacement.
namespace storage_report_format_cases {
struct GroupedPunctuation final : std::numpunct<char> {
    char do_thousands_sep() const override { return ','; }
    char do_decimal_point() const override { return ','; }
    std::string do_grouping() const override { return "\3"; }
};

struct Sink final : std::streambuf {
    std::string bytes;
    int writes{}, syncs{}, imbues{};
    bool short_write{}, throw_write{}, fail_sync{}, throw_sync{};
    std::streamsize xsputn(const char* data, std::streamsize count) override {
        ++writes;
        if (throw_write) throw std::runtime_error("storage sink write failure");
        const auto n = short_write ? std::min(count, std::streamsize{2}) : count;
        bytes.append(data, static_cast<std::size_t>(n));
        return n;
    }
    int_type overflow(int_type value) override {
        if (traits_type::eq_int_type(value, traits_type::eof())) return traits_type::not_eof(value);
        const char ch = traits_type::to_char_type(value);
        return xsputn(&ch, 1) == 1 ? value : traits_type::eof();
    }
    int sync() override {
        ++syncs;
        if (throw_sync) throw std::runtime_error("storage sink sync failure");
        return fail_sync ? -1 : 0;
    }
    void imbue(const std::locale&) override { ++imbues; }
};

struct FormatState {
    std::ios_base::fmtflags flags;
    std::streamsize precision, width;
    char fill;
    std::locale locale;
    std::ostream* tie;
    std::streambuf* buffer;
    std::ios_base::iostate exceptions;
    explicit FormatState(std::ostream& out)
        : flags(out.flags()), precision(out.precision()), width(out.width()), fill(out.fill()),
          locale(out.getloc()), tie(out.tie()), buffer(out.rdbuf()), exceptions(out.exceptions()) {}
    bool unchanged(std::ostream& out) const {
        return flags == out.flags() && precision == out.precision() && width == out.width()
            && fill == out.fill() && locale == out.getloc() && tie == out.tie()
            && buffer == out.rdbuf() && exceptions == out.exceptions();
    }
};

struct GlobalLocale {
    std::locale old;
    explicit GlobalLocale(const std::locale& value) : old(std::locale::global(value)) {}
    ~GlobalLocale() { std::locale::global(old); }
    GlobalLocale(const GlobalLocale&) = delete;
    GlobalLocale& operator=(const GlobalLocale&) = delete;
};

inline void pollute(std::ostream& out, const std::locale& locale) {
    out.imbue(locale);
    out << std::hex << std::showbase << std::showpos << std::uppercase << std::boolalpha
        << std::internal << std::scientific << std::setprecision(1) << std::setfill('#');
    out.width(4096);
}

inline mqb::StorageInventory fixture() {
    mqb::StorageInventory inventory;
    inventory.artifact_root = ".mqb";
    inventory.root_exists = true;
    mqb::StorageEntry entry;
    entry.relative_path = "bin/a.exe";
    entry.kind = mqb::StorageEntryKind::file;
    entry.logical_bytes = 1234567;
    entry.allocated_bytes = 1236992;
    entry.hard_links = 16;
    entry.physical_id = "volume:1";
    entry.protected_path = false;
    entry.references = {0, 16};
    inventory.entries.push_back(entry);
    inventory.references.resize(17);
    inventory.references[0].kind = "link";
    inventory.references[0].tool_version = "valid \xe6\x97\xa5";
    return inventory;
}

inline int run() {
    int failures = 0, checks = 0;
    const auto expect = [&](bool ok, std::string_view message) {
        ++checks;
        if (!ok) { ++failures; std::cerr << "FAIL: storage format: " << message << '\n'; }
    };
    const auto render = [](std::ostream& out, const mqb::StorageInventory& value, bool json) {
        return mqb::app::diagnostics::write_storage_report(out, value, json);
    };
    const auto inventory = fixture();
    const std::locale grouped(std::locale::classic(), new GroupedPunctuation);
    std::ostringstream json_reference, text_reference;
    json_reference.imbue(std::locale::classic());
    text_reference.imbue(std::locale::classic());
    expect(render(json_reference, inventory, true), "classic JSON succeeds");
    expect(render(text_reference, inventory, false), "classic text succeeds");
    const auto parsed = mqb::json::parse(json_reference.str());
    expect(parsed.has_value(), "classic JSON parses");
    if (parsed) {
        expect(parsed->object.at("totals").object.at("logical_bytes").scalar == "1234567",
               "raw byte count is decimal");
        expect(parsed->object.at("unique_file_allocated_bytes").scalar == "1236992",
               "physical allocation is unchanged");
        expect(!parsed->object.at("deletion_authorized").boolean
                   && parsed->object.at("reclaimable_bytes").kind == mqb::json::Kind::null_value,
               "format fix grants no deletion or reclamation claim");
    }
    for (const bool json : {true, false}) {
        const auto reference = json ? json_reference.str() : text_reference.str();
        for (const bool decorated : {false, true}) {
            Sink sink, tied_sink;
            std::ostream out(&sink), tied(&tied_sink);
            if (decorated) pollute(out, grouped); else out.imbue(grouped);
            out.tie(&tied);
            const FormatState saved(out);
            const auto buffer_locale = sink.getloc();
            const int imbues_before = sink.imbues;
            expect(render(out, inventory, json), "grouped/decorated report succeeds");
            expect(sink.bytes == reference, "locale/base/width cannot alter report bytes");
            expect(saved.unchanged(out), "caller formatting and stream connections are unchanged");
            expect(sink.getloc() == buffer_locale && sink.imbues == imbues_before,
                   "shared streambuf locale is not re-imbued");
            expect(tied_sink.syncs > 0, "existing tied-stream flush is respected");
        }
    }
    {
        // The report's local unit-conversion stream must not inherit this locale either.
        GlobalLocale global(grouped);
        std::ostringstream json, text;
        expect(render(json, inventory, true) && json.str() == json_reference.str(), "global locale JSON independence");
        expect(render(text, inventory, false) && text.str() == text_reference.str(), "global locale GB/GiB independence");
    }
    {
        auto extreme = inventory;
        extreme.entries[0].logical_bytes = (std::numeric_limits<std::uint64_t>::max)();
        extreme.entries[0].allocated_bytes.reset();
        extreme.issues.push_back({"missing", "unavailable", 12345});
        std::ostringstream out;
        pollute(out, grouped);
        expect(render(out, extreme, true), "maximum and unavailable values render");
        const auto value = mqb::json::parse(out.str());
        expect(value.has_value(), "extreme JSON parses without grouping or hex prefixes");
        {
            expect(value && value->object.at("totals").object.at("logical_bytes").scalar == "18446744073709551615",
                   "full uint64 range is exact");
            expect(value && value->object.at("totals").object.at("path_allocated_bytes").kind == mqb::json::Kind::null_value,
                   "unknown remains null, not zero");
            expect(value && value->object.at("issues").array[0].object.at("native_code").scalar == "12345",
                   "native error code remains decimal");
            expect(value && value->object.at("entries").array[0].object.at("reference_ids").array[1].scalar == "16",
                   "reference IDs remain decimal");
        }
    }
    {
        Sink sink;
        std::ostream out(&sink);
        pollute(out, grouped);
        out.setf(std::ios_base::unitbuf);
        const FormatState saved(out);
        expect(render(out, inventory, true) && sink.bytes == json_reference.str(), "unitbuf report remains canonical");
        expect(saved.unchanged(out), "unitbuf setting is preserved");
        expect(sink.syncs > 1, "requested unit buffering remains active");
    }
    for (const auto state : {std::ios_base::failbit, std::ios_base::badbit, std::ios_base::eofbit}) {
        Sink sink;
        std::ostream out(&sink);
        pollute(out, grouped);
        out.setstate(state);
        const FormatState saved(out);
        expect(!render(out, inventory, true), "pre-existing failure is not cleared");
        expect(sink.bytes.empty() && (out.rdstate() & state) != 0 && saved.unchanged(out),
               "failed destination emits nothing and preserves state/format");
    }
    {
        std::ostream out(nullptr);
        expect(!render(out, inventory, true) && out.bad(), "null destination remains failed");
    }
    for (int fault = 0; fault < 4; ++fault) {
        for (const bool throwing : {false, true}) {
            Sink sink;
            sink.short_write = fault == 0; sink.throw_write = fault == 1;
            sink.fail_sync = fault == 2; sink.throw_sync = fault == 3;
            std::ostream out(&sink);
            pollute(out, grouped);
            if (throwing) out.exceptions(std::ios_base::badbit);
            const FormatState saved(out);
            bool returned = false, result = true, caught = false;
            std::string error;
            try { result = render(out, inventory, true); returned = true; }
            catch (const std::exception& value) { caught = true; error = value.what(); }
            expect(throwing ? caught : (returned && !result), "sink error obeys original exception mask");
            expect(out.bad(), "sink failure reaches original ostream");
            expect(saved.unchanged(out), "sink failure preserves original format/mask");
            if (throwing && (fault == 1 || fault == 3)) {
                expect(error == (fault == 1 ? "storage sink write failure" : "storage sink sync failure"),
                       "original streambuf exception is not replaced");
            }
            if (fault < 2) expect(sink.writes == 1, "failed write is not retried");
            else expect(sink.syncs == 1 && sink.bytes == json_reference.str(), "failed final flush is not retried");
        }
    }
    {
        auto invalid = inventory;
        invalid.references[0].tool_version = "\xff";
        Sink sink;
        std::ostream out(&sink);
        pollute(out, grouped);
        const FormatState saved(out);
        bool rejected = false;
        try { (void)render(out, invalid, true); } catch (const std::runtime_error&) { rejected = true; }
        expect(rejected, "invalid UTF-8 is still rejected");
        expect(saved.unchanged(out), "validation exception does not mutate caller formatting");
    }
    std::cout << "storage report formatting: " << checks << " checks, " << failures << " failures\n";
    return failures;
}
} // namespace storage_report_format_cases
