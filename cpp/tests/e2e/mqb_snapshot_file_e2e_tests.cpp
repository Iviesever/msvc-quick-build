#include "mqb/platform/windows/LinkFactSnapshotFile.hpp"
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>
#include <barrier>
#ifdef _WIN32
#include "mqb/platform/windows/WindowsProcessRunner.hpp"
#include "mqb/platform/windows/CommandLine.hpp"
#include "../../src/platform/windows/StorageReadPrimitives.hpp"
#endif

namespace {
namespace fs = std::filesystem;
using namespace mqb;
using namespace mqb::platform::windows;
unsigned checks{};
std::ofstream log_file;
void check(bool ok, const char* name) {
    ++checks;
    if (log_file.is_open()) { log_file << checks << ' ' << (ok ? "PASS " : "FAIL ") << name << '\n'; log_file.flush(); }
    if (!ok) throw std::runtime_error(name);
}
std::string bytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary); return {std::istreambuf_iterator<char>{in}, {}};
}
void put(const fs::path& p, std::string_view text) {
    std::ofstream out(p, std::ios::binary); out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close(); check(!out.fail(), "test fixture write");
}
#ifdef _WIN32
void run() {
    const auto root = fs::current_path() / "storage-evidence" / "snapshot-file";
    check(!fs::exists(root), "fresh retained evidence directory"); fs::create_directories(root);
    log_file.open(root / "checks.txt", std::ios::binary);
    LinkFactSnapshot v; v.output = "historical-not-current.exe"; v.cache_file = "historical.linkcache";
    v.signature = {9007199254740993ULL, 42}; v.capture_label = "caller annotation";
    const auto text = encode_link_fact_snapshot(v); check(text.has_value(), "valid fixture codec");
    const auto path = root / fs::path{L"history space \u65e5.json"};
    const auto created = create_link_fact_snapshot_file(path, v);
    check(created && created->document_bytes == text->size() && bytes(path) == *text, "create exact JSON bytes");
    const auto loaded = read_link_fact_snapshot_file(path);
    check(loaded && *loaded == v && !loaded->deletion_authorized && !loaded->current_content_verified, "strict historical roundtrip without authority");
    auto other = v; other.capture_label = "different caller";
    const auto conflict = create_link_fact_snapshot_file(path, other);
    check(!conflict && conflict.error().code == LinkFactFileErrorCode::already_exists &&
        !conflict.error().file_created && conflict.error().transferred_bytes == 0 && bytes(path) == *text, "existing file never overwritten");
    auto bad = v; bad.linked = true;
    const auto invalid = create_link_fact_snapshot_file(root / "invalid-record.json", bad);
    check(!invalid && invalid.error().codec_error.has_value() && !fs::exists(root / "invalid-record.json"), "invalid record causes no file creation");
    for (const auto& p : std::vector<fs::path>{fs::path{"relative.json"}, root / "../escape.json", root / "file:stream",
         root / "NUL", root / "trailing.", root / "trailing ", fs::path{L"\\\\server\\share\\file"}, fs::path{L"\\\\?\\C:\\file"}}) {
        const auto w = create_link_fact_snapshot_file(p, v); const auto r = read_link_fact_snapshot_file(p);
        check(!w && !r && w.error().stage == LinkFactFileStage::input && r.error().stage == LinkFactFileStage::input,
              "ambiguous/device/traversal path refused before native IO");
    }
    for (const wchar_t c : {wchar_t{0}, wchar_t{0xd800}, wchar_t{0xdc00}}) {
        auto n = path.native(); n.push_back(c); n += L"bad";
        check(!create_link_fact_snapshot_file(fs::path{n}, v) && !read_link_fact_snapshot_file(fs::path{n}), "NUL/invalid UTF16 refused");
    }
    auto deep = root; for (unsigned i = 0; i < 129; ++i) deep /= "p";
    auto depth = create_link_fact_snapshot_file(deep / "leaf.json", v);
    check(!depth && depth.error().code == LinkFactFileErrorCode::limit_exceeded, "ancestor work bounded before creation");
    check(!create_link_fact_snapshot_file(root / "absent" / "leaf.json", v) && !fs::exists(root / "absent"), "missing parent not created");
    check(!read_link_fact_snapshot_file(root) && !read_link_fact_snapshot_file(root / "missing.json"), "directory/missing leaf refused");
    const auto corrupt = root / "corrupt.json";
    for (const auto& s : std::vector<std::string>{std::string{}, text->substr(0, text->size()-1), *text+"x", "\xef\xbb\xbf"+*text}) {
        put(corrupt, s); const auto r = read_link_fact_snapshot_file(corrupt);
        check(!r && r.error().code == LinkFactFileErrorCode::invalid_snapshot && r.error().codec_error.has_value() && bytes(corrupt) == s,
              "strict codec refusal preserves corrupt input");
    }
    const auto oversized = root / "oversized.json";
    put(oversized, std::string(LinkFactSnapshotLimits::document_bytes + 1, ' '));
    const auto big = read_link_fact_snapshot_file(oversized);
    check(!big && big.error().code == LinkFactFileErrorCode::limit_exceeded && big.error().transferred_bytes == 0, "oversized file refused before content read");
    const auto padded = root / "exact-budget.json";
    put(padded, *text + std::string(LinkFactSnapshotLimits::document_bytes-text->size(), ' '));
    check(read_link_fact_snapshot_file(padded).has_value(), "exact byte budget with valid JSON accepted");
    const auto name = detail::native_path(path);
    {
        detail::Handle writer{::CreateFileW(name.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        check(writer.value != INVALID_HANDLE_VALUE, "conflicting writer fixture");
        const auto r = read_link_fact_snapshot_file(path);
        check(!r && r.error().native_code == ERROR_SHARING_VIOLATION, "read refuses concurrent writer");
    }
    check(read_link_fact_snapshot_file(path).has_value(), "failed-read handles released");
    const auto alias = root / "hardlink.json";
    check(::CreateHardLinkW(alias.c_str(), path.c_str(), nullptr) != 0, "hardlink fixture");
    const auto alias_read = read_link_fact_snapshot_file(alias);
    check(alias_read && loaded && *alias_read == *loaded && !create_link_fact_snapshot_file(alias, other) && bytes(path) == *text, "hardlink read not overwrite authority");
    const auto racers = root / "race.json";
    std::barrier start{2};
    using Result = std::expected<LinkFactFileReceipt, LinkFactFileError>;
    std::optional<Result> a, b; std::exception_ptr ea, eb;
    auto work = [&](const LinkFactSnapshot& s, std::optional<Result>& result, std::exception_ptr& error) {
        start.arrive_and_wait(); try { result = create_link_fact_snapshot_file(racers, s); } catch (...) { error = std::current_exception(); }
    };
    std::thread t1([&]{work(v, a, ea);}), t2([&]{work(other, b, eb);}); t1.join(); t2.join();
    check(!ea && !eb && a && b && a->has_value() != b->has_value(), "two simultaneous creators have exactly one winner");
    const auto& loser = a->has_value() ? b->error() : a->error();
    check(!loser.file_created && loser.transferred_bytes == 0, "loser did not truncate or write");
    const auto winner = read_link_fact_snapshot_file(racers);
    check(winner && *winner == (a->has_value() ? v : other), "race winner retained complete snapshot");
    const auto external = root / "external"; fs::create_directory(external); put(external / "sentinel.json", *text);
    wchar_t system[32768]{}; const auto n = ::GetSystemDirectoryW(system, 32768);
    check(n && n < 32768, "system command directory");
    auto launch_text = [](fs::path p) { p.make_preferred(); return utf16_to_utf8(p.wstring()).value(); };
    process::ProcessSpec spec; spec.executable = fs::path{system} / "cmd.exe"; spec.working_directory = root;
    spec.arguments = {"/d", "/c", "mklink", "/J", launch_text(root / "junction"), launch_text(external)};
    WindowsProcessRunner runner; const auto junction = runner.run(spec);
    check(junction && junction->exit_code == 0, "real junction created");
    const auto wr = create_link_fact_snapshot_file(root / "junction" / "new.json", v);
    const auto rd = read_link_fact_snapshot_file(root / "junction" / "sentinel.json");
    check(!wr && !rd && wr.error().code == LinkFactFileErrorCode::reparse_rejected && rd.error().code == LinkFactFileErrorCode::reparse_rejected,
          "reparse ancestor rejected for read and create");
    check(!read_link_fact_snapshot_file(root / "junction") && !create_link_fact_snapshot_file(root / "junction", v), "reparse leaf refused");
    check(!fs::exists(external / "new.json") && bytes(external / "sentinel.json") == *text, "junction target untouched");
    put(root / "completion.txt", "completed; historical IO only; no delete authority\n");
}
#endif
}
int main() {
#ifdef _WIN32
    try { run(); std::cout << "snapshot file Windows checks: " << checks << " passed\n"; }
    catch (const std::exception& e) { std::cerr << "check " << checks << ": " << e.what() << '\n'; return 1; }
#else
    std::cerr << "Windows/NTFS file tests require Windows; not executed\n"; return 2;
#endif
}
