#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "mqb/core/BuildSignature.hpp"
#include "mqb/core/LinkCache.hpp"
#include "mqb/core/LinkCacheFile.hpp"
#include "mqb/core/LinkOptions.hpp"
#include "mqb/core/LinkerIdentity.hpp"

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