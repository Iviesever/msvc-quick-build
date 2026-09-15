#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "mqb/core/BuildSignature.hpp"
#include "mqb/core/LinkCache.hpp"
#include "mqb/core/LinkCacheFile.hpp"
#include "mqb/core/LinkOptions.hpp"
#include "mqb/core/LinkerIdentity.hpp"
#include "mqb/core/PerformanceEvidence.hpp"

namespace {

namespace fs = std::filesystem;
int failures = 0;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

struct TempTree {
    fs::path root;
    ~TempTree() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
};

using Work = mqb::performance::WorkKind;
using Evidence = mqb::performance::EvidenceSnapshot;
constexpr std::array children{
    Work::link_cache_serialize, Work::link_cache_prepare,
    Work::link_cache_stream, Work::link_cache_install};
constexpr std::size_t link_index = static_cast<std::size_t>(mqb::performance::CacheKind::link);

std::chrono::nanoseconds work(const Evidence& evidence, const Work kind) {
    return evidence.work[static_cast<std::size_t>(kind)];
}

void check_subspans(const Evidence& evidence) {
    std::chrono::nanoseconds disjoint{};
    for (const auto child : children) {
        expect(work(evidence, child).count() >= 0, "subspan duration cannot be negative");
        disjoint += work(evidence, child);
    }
    expect(disjoint <= work(evidence, Work::link_cache_write),
           "disjoint save children must fit inside the inclusive save interval");
    expect(work(evidence, Work::link_cache_write_payload) + work(evidence, Work::link_cache_flush)
               <= work(evidence, Work::link_cache_stream),
           "payload and flush are nested in stream, not additive wall time");
}

std::string file_bytes(const fs::path& file) {
    std::ifstream stream{file, std::ios::binary};
    expect(bool(stream), "comparison file must be readable");
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

void test_save_evidence(const fs::path& root, const mqb::LinkCacheEntry& entry) {
    using mqb::performance::Activation;
    using mqb::performance::Collector;
    const auto plain_file = root / "plain.linkcache";
    Collector inactive;
    const auto plain = mqb::LinkCacheFile::save(plain_file, entry);
    expect(plain.has_value(), "unobserved save succeeds");
    const auto plain_bytes = file_bytes(plain_file);
    expect(!plain_bytes.empty(), "comparison is not two absent files");
    expect(inactive.snapshot().cache_files_written[link_index] == 0,
           "inactive collector must not receive writes");
    for (const auto child : children) {
        expect(work(inactive.snapshot(), child).count() == 0, "inactive subspans stay zero");
    }

    Collector observed;
    const auto observed_file = root / "observed.linkcache";
    {
        Activation activation{observed};
        expect(mqb::LinkCacheFile::save(observed_file, entry).has_value(), "observed create succeeds");
        expect(mqb::LinkCacheFile::save(observed_file, entry).has_value(), "observed replacement succeeds");
    }
    const auto snapshot = observed.snapshot();
    check_subspans(snapshot);
    expect(file_bytes(observed_file) == plain_bytes, "observation preserves exact cache format bytes");
    expect(snapshot.cache_files_written[link_index] == 2, "nested scopes do not double count writes");
    expect(snapshot.cache_bytes_written[link_index] == 2 * plain_bytes.size(),
           "nested scopes do not double count payload bytes");
    expect(work(snapshot, Work::link_cache_stream).count() > 0, "actual stream lifetime is observed");
    const auto loaded = mqb::LinkCacheFile::load(observed_file);
    expect(loaded && loaded->has_value(), "observed cache remains loadable");

    auto oversized = entry;
    oversized.linker.version.assign(4u * 1024u * 1024u + 1u, 'x');
    Collector rejected;
    const auto invalid_file = root / "invalid.linkcache";
    {
        Activation activation{rejected};
        const auto result = mqb::LinkCacheFile::save(invalid_file, oversized);
        expect(!result && result.error().code == mqb::LinkCacheFileErrorCode::file_write_failed,
               "serialization retains its original error category");
    }
    check_subspans(rejected.snapshot());
    expect(!fs::exists(invalid_file), "serialization failure must not install a cache");
    for (const auto child : {Work::link_cache_prepare, Work::link_cache_stream,
                             Work::link_cache_write_payload, Work::link_cache_flush, Work::link_cache_install}) {
        expect(work(rejected.snapshot(), child).count() == 0, "unreached phases are not invented");
    }
    expect(rejected.snapshot().cache_files_written[link_index] == 0, "serialization failure writes nothing");

    const auto blocker = root / "not-a-directory";
    { std::ofstream stream{blocker}; stream << "keep"; }
    Collector preparation;
    {
        Activation activation{preparation};
        const auto result = mqb::LinkCacheFile::save(blocker / "cache", entry);
        expect(!result && result.error().code == mqb::LinkCacheFileErrorCode::file_write_failed,
               "directory failure retains its original error category");
    }
    check_subspans(preparation.snapshot());
    expect(preparation.snapshot().cache_files_written[link_index] == 0, "directory failure writes nothing");
    expect(work(preparation.snapshot(), Work::link_cache_stream).count() == 0 &&
           work(preparation.snapshot(), Work::link_cache_install).count() == 0,
           "directory failure never enters stream or install");
    expect(file_bytes(blocker) == "keep", "directory failure does not damage blocking file");

    const auto directory = root / "nonempty.linkcache";
    fs::create_directories(directory);
    { std::ofstream stream{directory / "keep.txt"}; stream << "untouched"; }
    const auto unobserved_error = mqb::LinkCacheFile::save(directory, entry);
    Collector installation;
    {
        Activation activation{installation};
        const auto result = mqb::LinkCacheFile::save(directory, entry);
        expect(!result && !unobserved_error &&
               result.error().code == mqb::LinkCacheFileErrorCode::replace_failed &&
               result.error().message == unobserved_error.error().message &&
               result.error().file == unobserved_error.error().file,
               "observed install failure preserves original diagnostics and identity");
    }
    check_subspans(installation.snapshot());
    expect(installation.snapshot().cache_files_written[link_index] == 1,
           "failed install still records the actual temporary write");
    expect(file_bytes(directory / "keep.txt") == "untouched", "failed install preserves destination contents");
    for (const auto& item : fs::directory_iterator(root)) {
        expect(!item.path().filename().string().starts_with("nonempty.linkcache.tmp."),
               "failed install retains original temporary-file cleanup");
    }
}

} // namespace

int main() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    TempTree tree{
        .root = fs::temp_directory_path() / ("mqb_link_cache_" + std::to_string(unique)),
    };
    fs::create_directories(tree.root);

    const std::vector<fs::path> objects{"obj/main.cpp.obj", "obj/math.cpp.obj"};
    const std::vector<fs::path> libraries{"vendor/math.lib", "vendor/codec.lib"};
    const std::vector<fs::path> file_inputs{"exports/plugin.def"};
    const fs::path output{"bin/plugin.dll"};
    const std::vector<fs::path> side_outputs{"bin/plugin.lib", "bin/plugin.exp"};
    const mqb::LinkerIdentity linker{
        .linker = "C:/msvc/link.exe",
        .version = "14.51",
        .binary_stamp = "stamp-a",
    };
    mqb::LinkOptions options;
    options.target_kind = mqb::TargetKind::dynamic_library;
    options.library_directories = {"vendor"};
    options.libraries = {"math.lib", "codec.lib"};
    options.additional_arguments = {"/DEF:exports/plugin.def"};
    const auto signature = mqb::BuildSignature::for_link(
        objects, libraries, output, linker, options);
    const mqb::LinkCacheEntry entry{
        .linker = linker,
        .signature = signature,
        .objects = objects,
        .output = output,
        .libraries = libraries,
        .file_inputs = file_inputs,
        .side_outputs = side_outputs,
    };
    test_save_evidence(tree.root, entry);

    const fs::path file = tree.root / "cache" / "plugin.linkcache";
    const auto missing = mqb::LinkCacheFile::load(file);
    expect(missing.has_value() && !missing->has_value(),
           "missing link cache should be a normal cache miss");

    const auto saved = mqb::LinkCacheFile::save(file, entry);
    expect(saved.has_value(), "valid link cache should save");
    const auto loaded = mqb::LinkCacheFile::load(file);
    expect(loaded.has_value() && loaded->has_value(), "saved link cache should load");
    if (loaded && *loaded) {
        expect((*loaded)->linker.linker == linker.linker, "linker path should round-trip");
        expect((*loaded)->linker.version == linker.version, "linker version should round-trip");
        expect((*loaded)->linker.binary_stamp == linker.binary_stamp, "linker stamp should round-trip");
        expect((*loaded)->signature == signature, "link signature should round-trip");
        expect((*loaded)->objects == objects, "object inputs should round-trip");
        expect((*loaded)->libraries == libraries, "resolved library inputs should round-trip");
        expect((*loaded)->file_inputs == file_inputs,
               "generic linker file inputs should round-trip in cache v4");
        expect((*loaded)->output == output, "link output should round-trip");
        expect((*loaded)->side_outputs == side_outputs,
               "observed linker side outputs should round-trip in cache v4");
    }

    {
        std::fstream stream{file, std::ios::binary | std::ios::in | std::ios::out};
        stream.seekp(8, std::ios::beg);
        const char old_version[4]{1, 0, 0, 0};
        stream.write(old_version, 4);
    }
    const auto old_version = mqb::LinkCacheFile::load(file);
    expect(!old_version.has_value(), "unsupported link-cache format should be rejected safely");
    if (!old_version) {
        expect(old_version.error().code == mqb::LinkCacheFileErrorCode::unsupported_version,
               "unsupported cache version should report unsupported_version");
    }

    const auto restored_after_version = mqb::LinkCacheFile::save(file, entry);
    expect(restored_after_version.has_value(), "link cache should upgrade by safe replacement");

    {
        std::fstream stream{file, std::ios::binary | std::ios::in | std::ios::out};
        char bad = 'X';
        stream.write(&bad, 1);
    }
    const auto bad_magic = mqb::LinkCacheFile::load(file);
    expect(!bad_magic.has_value(), "corrupt link-cache magic should be rejected");
    if (!bad_magic) {
        expect(bad_magic.error().code == mqb::LinkCacheFileErrorCode::invalid_magic,
               "corrupt magic should report invalid_magic");
    }

    const auto restored = mqb::LinkCacheFile::save(file, entry);
    expect(restored.has_value(), "link cache should be replaceable after corruption");
    std::error_code error_code;
    const auto size = fs::file_size(file, error_code);
    expect(!error_code && size > 8, "saved link cache should be non-trivial");
    if (!error_code && size > 8) {
        fs::resize_file(file, size - 5, error_code);
        expect(!error_code, "test should be able to truncate cache file");
    }
    const auto truncated = mqb::LinkCacheFile::load(file);
    expect(!truncated.has_value(), "truncated link cache should be rejected");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "mqb_link_cache_file_tests passed\n";
    return 0;
}