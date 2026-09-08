#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#include "mqb/core/ArchiveCacheFile.hpp"

namespace {
namespace fs = std::filesystem;
constexpr std::size_t limit = 64u * 1024u * 1024u;
int failures = 0;
void expect(const bool condition, const std::string_view message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
struct TempTree {
    fs::path root = fs::temp_directory_path() / ("mqb_archive_transport_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    TempTree() { fs::create_directories(root); }
    ~TempTree() { std::error_code ec; fs::remove_all(root, ec); }
};
std::string path_text(const fs::path& path) {
    const auto bytes = path.lexically_normal().generic_u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
std::string legacy_bytes(const mqb::ArchiveCacheEntry& entry) {
    // Exact v1 writer grammar; keep a compatibility oracle independent of the
    // new bounded buffer, including quotes, escapes and whitespace.
    std::ostringstream out;
    out << "MQBARCHIVE 1\n" << std::quoted(path_text(entry.librarian.librarian)) << '\n'
        << std::quoted(entry.librarian.version) << '\n'
        << std::quoted(entry.librarian.binary_stamp) << '\n'
        << entry.signature.digest().high << ' ' << entry.signature.digest().low << '\n'
        << std::quoted(path_text(entry.output)) << '\n' << entry.objects.size() << '\n';
    for (const auto& object : entry.objects) out << std::quoted(path_text(object)) << '\n';
    return out.str();
}
std::string read_bytes(const fs::path& file) {
    std::ifstream in{file, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}
void write_bytes(const fs::path& file, const std::string_view bytes) {
    std::ofstream out{file, std::ios::binary | std::ios::trunc};
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    expect(static_cast<bool>(out), "fixture write must succeed");
}
void expect_entry(const fs::path& file, const mqb::ArchiveCacheEntry& entry) {
    const auto loaded = mqb::ArchiveCacheFile::load(file);
    expect(loaded && *loaded, "valid legacy-format archive must load");
    if (!loaded || !*loaded) return;
    const auto& got = **loaded;
    expect(got.librarian.librarian == entry.librarian.librarian
        && got.librarian.version == entry.librarian.version
        && got.librarian.binary_stamp == entry.librarian.binary_stamp
        && got.signature == entry.signature && got.objects == entry.objects
        && got.output == entry.output, "all fields, order and binary string bytes must round-trip");
}
void expect_corrupt(const fs::path& file, const std::string_view bytes) {
    write_bytes(file, bytes);
    const auto loaded = mqb::ArchiveCacheFile::load(file);
    expect(!loaded && loaded.error().code == mqb::ArchiveCacheFileErrorCode::corrupt_data,
           "malformed existing cache must be corrupt, never missing");
}
}

int main() {
    TempTree tree;
    const fs::path file = tree.root / "record.archivecache";
    mqb::ArchiveCacheEntry entry{
        .librarian = {.librarian = "tools/lib.exe", .version = "14.51",
                      .binary_stamp = std::string{"stamp\0", 6} + "\"\\\r\n"},
        .signature = mqb::BuildSignature::from_digest({42, 99}),
        .objects = {"obj/a.obj", fs::path{u8"obj/空 格.obj"}, "obj/third.obj"},
        .output = "bin/archive.lib",
    };
    const auto missing = mqb::ArchiveCacheFile::load(file);
    expect(missing && !*missing, "missing archive remains a normal miss");
    const auto directory = mqb::ArchiveCacheFile::load(tree.root);
    expect(!directory, "a directory must not be accepted as a cache miss or payload");
    std::uint32_t random = 0x159160u;
    for (unsigned trial = 0; trial < 256; ++trial) {
        auto varied = entry;
        varied.librarian.binary_stamp.clear();
        for (unsigned index = 0; index < trial; ++index) {
            random = random * 1664525u + 1013904223u;
            varied.librarian.binary_stamp.push_back(static_cast<char>(random >> 24));
        }
        expect(mqb::ArchiveCacheFile::save(file, varied).has_value(), "binary stamp fixture must save");
        expect(read_bytes(file) == legacy_bytes(varied), "bounded escaping must match legacy writer byte-for-byte");
        expect_entry(file, varied);
    }
    const auto original = legacy_bytes(entry);
    write_bytes(file, original);
    expect_entry(file, entry);
    expect(mqb::ArchiveCacheFile::save(file, entry).has_value(), "valid save should succeed");
    expect(read_bytes(file) == original, "new writer must retain exact v1 serialized bytes");
    write_bytes(file, original + " \r\n\t");
    expect_entry(file, entry);
    expect_corrupt(file, "");
    expect_corrupt(file, "WRONG 1\n");
    expect_corrupt(file, original.substr(0, original.size() - 3));
    expect_corrupt(file, original + "unexpected");
    expect_corrupt(file, original + std::string(1, '\0'));
    auto unsupported = original;
    unsupported.replace(11, 1, "2");
    write_bytes(file, unsupported);
    const auto old_version = mqb::ArchiveCacheFile::load(file);
    expect(!old_version && old_version.error().code == mqb::ArchiveCacheFileErrorCode::unsupported_version,
           "unsupported version must keep its typed error");
    expect_corrupt(file, "MQBARCHIVE 1\n\"lib\"\n\"v\"\n\"s\"\n1 2\n\"out\"\n100001\n");
    write_bytes(file, "x");
    fs::resize_file(file, limit + 1);
    const auto too_large = mqb::ArchiveCacheFile::load(file);
    expect(!too_large && too_large.error().code == mqb::ArchiveCacheFileErrorCode::corrupt_data,
           "over-limit file must be rejected before payload allocation/decoding");
    expect(mqb::ArchiveCacheFile::save(file, entry).has_value(), "valid save must repair corrupt cache");
    expect_entry(file, entry);
    fs::remove(file);
    const auto removed = mqb::ArchiveCacheFile::load(file);
    expect(removed && !*removed, "removed cache is a new miss");
    entry.librarian.binary_stamp = "replacement";
    expect(mqb::ArchiveCacheFile::save(file, entry).has_value(), "same pathname can be recreated");
    expect_entry(file, entry);
    const auto prior = read_bytes(file);

    // Escaping counts against the byte limit. An oversized save must preserve
    // the prior cache and must not even create a new destination directory.
    auto large = entry;
    large.librarian.binary_stamp.assign(limit / 2, '\\');
    const auto rejected = mqb::ArchiveCacheFile::save(file, large);
    expect(!rejected && rejected.error().code == mqb::ArchiveCacheFileErrorCode::file_write_failed,
           "escaped serialized bytes, not just input string length, enforce the write bound");
    expect(read_bytes(file) == prior, "over-limit save must not replace prior valid cache");
    large.librarian.binary_stamp.clear();
    large.objects.assign(100001, "a.obj");
    const auto new_path = tree.root / "untouched" / "cache";
    expect(!mqb::ArchiveCacheFile::save(new_path, large), "oversized object count must fail");
    expect(!fs::exists(new_path.parent_path()), "failed serialization must precede directory creation");
    large.objects = entry.objects;
    large.librarian.binary_stamp.clear();
    const auto overhead = legacy_bytes(large).size();
    large.librarian.binary_stamp.assign(limit - overhead, 'a');
    expect(mqb::ArchiveCacheFile::save(file, large).has_value(), "exact 64 MiB serialized boundary is writable");
    expect(fs::file_size(file) == limit, "exact-limit output has exactly the permitted size");
    expect_entry(file, large);
    large.librarian.binary_stamp.push_back('a');
    expect(!mqb::ArchiveCacheFile::save(new_path, large), "one byte past exact limit must fail");
    expect(!fs::exists(new_path.parent_path()), "byte-limit failure must not create directories");
    expect(fs::file_size(file) == limit, "failed save elsewhere leaves existing boundary cache intact");
    std::size_t files = 0;
    for (const auto& item : fs::directory_iterator{tree.root}) {
        ++files;
        expect(item.path() == file, "no temporary cache residue may remain");
    }
    expect(files == 1, "only the accepted cache file should exist");
    if (failures) { std::cerr << failures << " test(s) failed\n"; return 1; }
    std::cout << "mqb_archive_cache_file_tests passed\n";
}
