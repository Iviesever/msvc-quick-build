#include "../../../src/platform/windows/LinkFactFileTransfer.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
namespace f = mqb::platform::windows::detail::fact_file;
using mqb::LinkFactSnapshotLimits;
unsigned checks{};
void check(bool ok, const char* message) { ++checks; if (!ok) throw std::runtime_error(message); }
struct Fake {
    std::string data, output;
    std::size_t position{}, chunk{7}, fail_at{std::numeric_limits<std::size_t>::max()};
    unsigned reads{}, writes{}, flushes{}, closes{}, sizes{};
    bool zero{}, excessive{}, flush_error{}, close_error{}, size_error{}, changed{};
    std::optional<std::uint64_t> declared;
    std::expected<std::size_t, std::uint32_t> write(std::span<const char> bytes) {
        ++writes;
        if (position >= fail_at) return std::unexpected(112U); // synthetic disk-full
        if (excessive) return bytes.size() + 1;
        if (zero) return 0;
        const auto n = std::min({chunk, bytes.size(), fail_at - position});
        output.append(bytes.data(), n); position += n; return n;
    }
    std::expected<std::size_t, std::uint32_t> read(std::span<char> bytes) {
        ++reads;
        if (position >= fail_at) return std::unexpected(23U);
        if (excessive) return bytes.size() + 1;
        if (zero) return 0;
        const auto n = std::min({chunk, bytes.size(), data.size() - position, fail_at - position});
        std::copy_n(data.data() + position, n, bytes.data()); position += n; return n;
    }
    std::expected<std::uint64_t, std::uint32_t> size() {
        if (size_error) return std::unexpected(6U);
        return declared.value_or(data.size()) + (changed && sizes++ ? 1 : 0);
    }
    std::expected<void, std::uint32_t> flush() { ++flushes; if (flush_error) return std::unexpected(29U); return {}; }
    std::expected<void, std::uint32_t> close() { ++closes; if (close_error) return std::unexpected(6U); return {}; }
};
void run() {
    const std::string text(150000, 'x');
    for (const std::size_t chunk : {1U, 7U, 65536U, 1000000U}) {
        Fake io; io.chunk = chunk;
        auto w = f::write_document(io, text);
        check(w && w->document_bytes == text.size() && io.output == text, "short writes concatenate exact document");
        check(io.flushes == 1 && io.closes == 1, "one flush/close on success");
        Fake in; in.chunk = chunk; in.data = text;
        auto r = f::read_document(in);
        check(r && *r == text && in.closes == 1 && in.flushes == 0, "short reads and EOF probe");
    }
    for (const std::size_t offset : {0U, 7U, 65536U}) {
        Fake io; io.fail_at = offset; io.close_error = true;
        auto w = f::write_document(io, text);
        check(!w && w.error().code == f::Code::transfer_failed && w.error().native_code == 112 &&
              w.error().file_created && w.error().transferred_bytes == offset && w.error().close_error == 6,
              "primary write failure, cleanup failure and partial byte count survive");
        check(io.output == text.substr(0, offset) && io.flushes == 0 && io.closes == 1, "failed prefix kept; no flush/retry");
        Fake in; in.data = text; in.fail_at = offset;
        auto r = f::read_document(in);
        check(!r && r.error().native_code == 23 && r.error().transferred_bytes == offset && !r.error().file_created && in.closes == 1,
              "read failure retains offset and closes");
    }
    for (int mode = 0; mode < 4; ++mode) {
        Fake io; io.zero = mode == 0; io.excessive = mode == 1; io.flush_error = mode == 2; io.close_error = mode == 3;
        auto w = f::write_document(io, text);
        check(!w && io.closes == 1 && w.error().file_created, "no false write success for zero/excess/flush/close failure");
        check(w.error().stage == (mode < 2 ? f::Stage::write : mode == 2 ? f::Stage::flush : f::Stage::close), "precise failure stage");
    }
    for (int mode = 0; mode < 6; ++mode) {
        Fake in; in.data = text;
        if (mode == 0) in.declared = text.size() + 1;
        if (mode == 1) in.declared = text.size() - 1;
        in.changed = mode == 2; in.size_error = mode == 3; in.excessive = mode == 4; in.close_error = mode == 5;
        auto r = f::read_document(in);
        const auto code = mode == 0 ? f::Code::truncated : mode < 3 ? f::Code::changed : mode == 3 ?
            f::Code::unavailable : mode == 4 ? f::Code::transfer_failed : f::Code::close_failed;
        check(!r && r.error().code == code && in.closes == 1, "read refuses truncation/growth/metadata/invalid transfer/close");
    }
    Fake big; big.declared = LinkFactSnapshotLimits::document_bytes + 1;
    check(!f::read_document(big) && big.reads == 0 && big.closes == 1, "size refused before allocation/read");
    Fake exact; exact.data.assign(LinkFactSnapshotLimits::document_bytes, ' '); exact.chunk = 65536;
    check(f::read_document(exact).has_value(), "exact budget is admitted by transfer, not a codec approval");
    Fake empty;
    auto zero = f::read_document(empty);
    check(zero && zero->empty() && empty.reads == 1 && empty.closes == 1, "empty document still probes EOF; codec must refuse");
    Fake huge;
    check(!f::write_document(huge, std::string(LinkFactSnapshotLimits::document_bytes + 1, 'x')) && huge.writes == 0 && huge.closes == 1,
          "transfer upper bound rejects before write");
}
}
int main() {
    try { run(); std::cout << "snapshot transfer: " << checks << " checks passed (synthetic IO only)\n"; }
    catch (const std::exception& e) { std::cerr << "check " << checks << ": " << e.what() << '\n'; return 1; }
}
